#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}


#ifdef LAB_PGTBL

extern pte_t * walk(pagetable_t, uint64, int);
//在本部分中，需要增加一个系统调用，以返回用户空间虚拟地址中的 “脏页” 分布情况，脏页即被访问过的页面。
int//具体的调用方式为，用户输入检测的起始虚拟地址和检测页面数，内核在计算后通过指针返回用于表示脏页分布情况的位掩码。
sys_pgaccess(void)//简单来说，就是检测某个页面是否被访问的
{
  // lab pgtbl: your code here.
 
      uint64 va,dst;
     int n;
     if(argint(1, &n) < 0 || argaddr(0, &va) < 0 || argaddr(2, &dst) < 0)
         return -1;//va起始地址
     if(n > 64 || n < 0)
         return -1;
     uint64 bitmask = 0,mask = 1;
     pte_t *pte;
     pagetable_t pagetable = myproc()->pagetable;
     while(n > 0){//n检查页数
         pte = walk(pagetable,va,1);//指向页表项（Page Table Entry，PTE）的指针
         if(pte){
             if(*pte & PTE_A)//1<<6 表示访问过
                 bitmask |= mask;
             *pte = *pte & (~PTE_A);// 清除访问标记
         }
         mask <<= 1;
         va = (uint64)((char*)(va)+PGSIZE);
         n--;
     }
     if(copyout(pagetable,dst,(char *)&bitmask,sizeof(bitmask)) < 0)//将结果传给用户态
         return -1;

  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
