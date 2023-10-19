#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

static int nthread = 1;
static int round = 0;

struct barrier {
  pthread_mutex_t barrier_mutex;
  pthread_cond_t barrier_cond;
  int nthread;      // Number of threads that have reached this round of the barrier
  int round;     // Barrier round
} bstate;

static void
barrier_init(void)
{
  assert(pthread_mutex_init(&bstate.barrier_mutex, NULL) == 0);
  assert(pthread_cond_init(&bstate.barrier_cond, NULL) == 0);
  bstate.nthread = 0;
}

static void 
barrier()
{
  // YOUR CODE HERE
  // 你需要在这里添加你的代码

  // Block until all threads have called barrier() and
  // then increment bstate.round.
  // 阻塞，直到所有线程都调用了 barrier()，然后增加 bstate.round。

  // 获得 barrier_mutex 锁，用于互斥访问屏障状态
  pthread_mutex_lock(&bstate.barrier_mutex);

  // 如果还有线程没有达到屏障
  if (++bstate.nthread < nthread) {
    // 当前线程进入等待状态，释放 barrier_mutex 互斥锁
    // 在其他线程唤醒时重新获取锁
    pthread_cond_wait(&bstate.barrier_cond, &bstate.barrier_mutex);
  } else {
    // 如果所有线程都已经达到屏障

    bstate.nthread = 0; // 重置 nthread 计数为 0
    bstate.round++;     // 增加轮次 round
    // 唤醒所有休眠在 barrier_cond 条件变量上的线程
    pthread_cond_broadcast(&bstate.barrier_cond);
  }

  // 释放 barrier_mutex 互斥锁
  pthread_mutex_unlock(&bstate.barrier_mutex);
}

static void *
thread(void *xa)
{
  long n = (long) xa;
  long delay;
  int i;

  for (i = 0; i < 20000; i++) {
    int t = bstate.round;
    assert (i == t);
    barrier();
    usleep(random() % 100);
  }

  return 0;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  long i;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "%s: %s nthread\n", argv[0], argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);

  barrier_init();

  for(i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, thread, (void *) i) == 0);
  }
  for(i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  printf("OK; passed\n");
}
