#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}
//"buffer" 通常指的是用于存储网络数据包的内存区域或数据结构。这些数据包缓冲区被用于暂时存储从网络接口卡（NIC）接收到的数据包或要发送到网络的数据包。
int
e1000_transmit(struct mbuf *m)//描述符是一种元数据，它不存储实际的数据内容，而只是提供了数据的相关信息。
{
  acquire(&e1000_lock); // 获取 E1000 的锁，防止多进程同时发送数据出现 race
  // 尝试获取该锁，如果已经被其他进程占用，则会阻塞等待，直到获取到锁为止。这样可以保证同一时间只有一个进程可以访问 E1000 网卡的资源，避免数据混乱或丢失。

  uint32 ind = regs[E1000_TDT]; // 下一个可用的 buffer 的下标
  //regs 数组是一个映射了 E1000 网卡寄存器的内存区域，E1000_TDT 是一个常量，表示 Transmit Descriptor Tail Register 的下标
  //，该寄存器存储了下一个可用的发送 buffer 的下标！

  //传输描述符结构体     tx_ring是存储传输描述符的数组
  struct tx_desc *desc = &tx_ring[ind]; // 获取 buffer 的描述符，其中存储了关于该 buffer 的各种信息
  // tx_ring 数组是一个存储了发送描述符的内存区域，每个描述符包含其物理地址、长度、命令、状态等信息。

  // 如果该 buffer 中的数据还未传输完，则代表我们已经将环形 buffer 列表全部用完，缓冲区不足，返回错误
  if(!(desc->status & E1000_TXD_STAT_DD)) {
    release(&e1000_lock);//当硬件完成了对当前描述符的填充时，E1000_RXD_STAT_DD 被置为1，以通知驱动程序软件将其提取。
    return -1;
  }// 检查 desc 指向的描述符中的 status 字段是否包含 E1000_TXD_STAT_DD 这个位，该位表示该
  // buffer 中的数据是否已经被网卡发送完毕。如果没有包含，则说明该 buffer 还在使用中，不能被覆盖。由于我们使用了环形
  // buffer 列表，如果遇到这种情况，则说明我们已经没有空闲的 buffer 可以使用了，因此需要释放锁并返回 -1 表示错误。

  // 如果该下标仍有之前发送完毕但未释放的 mbuf，则释放
  if(tx_mbufs[ind]) {// 这几行的作用是检查 tx_mbufs 数组中 ind 下标对应的元素是否为空指针，
    mbuffree(tx_mbufs[ind]);//该数组是一个存储了与发送 buffer 对应的 mbuf 指针的内存区域。
    tx_mbufs[ind] = 0;//mbuf 是一个网络数据包的结构体，包含了数据内容和长度等信息。如果不为空指针，则说明该下标之前已经发送过一个 mbuf，
  }//并且已经被网卡发送完毕，但是还没有被释放。因此需要调用 mbuffree 函数来释放该 mbuf，并将 tx_mbufs 数组中 ind 下标对应的元素置为零。
  

  // 将要发送的 mbuf 的内存地址与长度填写到发送描述符中
  desc->addr = (uint64)m->head;
  desc->length = m->len;
  //将传入函数的参数 m 指向的 mbuf 结构体中的 head 和 len 字段分别赋值给 desc 指向的描述符中的 addr 和 length 字段。
  //head 字段是一个指向 mbuf 中数据内容的指针，len 字段是一个表示 mbuf 中数据长度的整数。这样就将要发送的 mbuf 的内存地址和长度告诉了网卡。

  // 设置参数，EOP 表示该 buffer 含有一个完整的 packet
  // RS 告诉网卡在发送完成后，设置 status 中的 E1000_TXD_STAT_DD 位，表示发送完成。
  desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
  //cmd 字段是一个表示发送 buffer 的命令参数的整数，E1000_TXD_CMD_EOP表示当前描述符是数据包的最后一个描述符。当数据包跨越多个描述符时，只有最后一个描述符应该设置这个标志。它告诉硬件，这是数据包的结束，可以进行传输。 
  //E1000_TXD_CMD_RS 是一个常量，表示要求网卡在发送完成后，设置 status 字段中的 E1000_TXD_STAT_DD 位，以便我们可以检查该 buffer 是否已经发送完毕。
//通常，E1000_TXD_CMD_RS 会与 E1000_TXD_CMD_EOP 一起使用，后者表示数据包的最后一个描述符。当数据包跨越多个描述符时，只有最后一个描述符需要同时设置 E1000_TXD_CMD_RS 和 E1000_TXD_CMD_EOP 标志，以指示传输完成并报告状态。

  // 保留新 mbuf 的指针，方便后续再次用到同一下标时释放。
  tx_mbufs[ind] = m;

  // 环形缓冲区内下标增加一。
  regs[E1000_TDT] = (regs[E1000_TDT] + 1) % TX_RING_SIZE;// Transmit Descriptor Tail (TDT) 寄存器


  release(&e1000_lock);
  return 0;
}// 调用 release 函数，传入一个指向 e1000_lock 的指针，该函数会释放该锁，让其他进程可以获取。然后返回 0 表示成功。


static void
e1000_recv(void)
{
  while(1) { // 每次 recv 可能接收多个包

    uint32 ind = (regs[E1000_RDT] + 1) % RX_RING_SIZE;//  ind 表示下一个要被网卡写入数据的接收 buffer 的下标。
    
    struct rx_desc *desc = &rx_ring[ind];//rx_ring 数组是一个存储了接收 buffer 描述符的内存区域，每个描述符包含了 buffer 的物理地址、长度、状态等信息。

    // 如果需要接收的包都已经接收完毕，则退出
    if(!(desc->status & E1000_RXD_STAT_DD)) {
      return;
    }//E1000_RXD_STAT_DD 这个位，表示该 buffer 中的数据是否已经被网卡写入完毕。如果没有包含，则说明该 buffer 还没有接收到数据，或者数据还没有完整地写入。
    //由于我们使用了环形 buffer 列表，如果遇到这种情况，则说明我们已经没有新的数据包可以接收了，因此需要退出循环并返回。

    rx_mbufs[ind]->len = desc->length;
    // 这一行的作用是将 desc 指向的描述符中的 length 字段赋值给 rx_mbufs 数组中 ind 下标对应的元素指向的 mbuf 结构体中的 len 字段。rx_mbufs 数组是一个存储了与接收 buffer 对应的 mbuf 指针的内存区域。length 字段是一个表示接收 buffer 中数据长度的整数，len 字段是一个表示 mbuf 中数据长度的整数。这样就将接收到的数据包的长度告诉了 mbuf。

    net_rx(rx_mbufs[ind]); // 传递给上层网络栈。上层负责释放 mbuf

    // 分配并设置新的 mbuf，供给下一次轮到该下标时使用
    rx_mbufs[ind] = mbufalloc(0); 
    desc->addr = (uint64)rx_mbufs[ind]->head;//head 字段是一个指向 mbuf 中数据内容的指针，addr 字段是一个表示接收 buffer 的物理地址的整数。这样就将新分配的 mbuf 的内存地址告诉了网卡。
    desc->status = 0;    //最后将 desc 指向的描述符中的 status 字段赋值为 0，表示该 buffer 可以被网卡写入数据。


    regs[E1000_RDT] = ind;
    // 这一行的作用是将 ind 变量赋值给 regs 数组中 E1000_RDT 下标对应的元素。这样就更新了最后一个被网卡写入数据的接收 buffer 的下标。
  }
}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
