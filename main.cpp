#include "RingQueue.h"
#include <sys/time.h>
#include <stdio.h>

#define NUM 10080000
#define MAX_THREAD_NUM 10

enum Method{
  MUTEX = 0,
  SPIN1,
  SPIN2,
  CAS,
  MIXED1,
  MIXED2,
  METHOD_NUM
};

const char* method_name[] = {
  "mutex","spin1","spin2","cas","mixed1","mixed2"
};

struct Param{
  int id;
  int method;
  int thread_num;
};

RingQueue<int> queue(0x10000); 
int input[NUM];
Param args[MAX_THREAD_NUM * 2];


void* PushThread(void* arg){
  Param * param = (Param*)arg;
  int id = param->id;
  int* p = input + id;
  int task = NUM / param->thread_num;
  for (int i = 0; i < task; i++){
    switch (param->method){
      case MUTEX:  while (queue.mutex_push(p) < 0);   break;
      case SPIN1:  while (queue.spin1_push(p) < 0);   break;
      case SPIN2:  while (queue.spin2_push(p) < 0);   break;
      case CAS:    while (queue.cas_push(p) < 0);     break;
      case MIXED1: while (queue.mixed1_push(p) < 0);  break;
      case MIXED2: while (queue.mixed2_push(p) < 0);  break;
      default: break;
    }
    p += param->thread_num;
  }
  return (void*)NULL;
}

void* PopThread(void* arg){
  Param * param = (Param*)arg;
  int task = NUM / param->thread_num;
  int* p;
  for (int i = 0; i < task; i++){
    switch (param->method){
      case MUTEX:  while ((p = queue.mutex_pop()) == NULL);   break;
      case SPIN1:  while ((p = queue.spin1_pop())  == NULL);  break;
      case SPIN2:  while ((p = queue.spin2_pop())  == NULL);  break;
      case CAS:    while ((p = queue.cas_pop())  == NULL);    break;
      case MIXED1: while ((p = queue.mixed1_pop())  == NULL); break;
      case MIXED2: while ((p = queue.mixed2_pop())  == NULL); break;
      default: break;
    }
    //*p = *p + 1;   
  }
  return (void*)NULL;
}

int Test(int thread_num, int method){

  pthread_t t[20];
  timeval start,end;
  long diff;

  queue.reset();

  for (int i = 0; i < thread_num; i++){
    args[i+thread_num].id = i;
    args[i+thread_num].method = method;
    args[i+thread_num].thread_num = thread_num;
    pthread_create(t+i+thread_num, NULL, PopThread, (void*)(args+i+thread_num));
  }

  gettimeofday(&start,NULL);

  for (int i = 0; i < thread_num; i++){
    args[i].id = i;
    args[i].method = method;
    args[i].thread_num = thread_num;
    pthread_create(t+i, NULL, PushThread, (void*)(args+i));
  }

  for (int i = 0; i < thread_num ; i++)
    pthread_join(t[i], NULL);

  for (int i = 0; i < thread_num ; i++)
    pthread_join(t[i + thread_num], NULL);

  gettimeofday(&end,NULL);

  diff = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;

  printf("%10ld", diff);

  return 0;
}

int main(){

  queue.init();

  /*
  // verify push/pop works 
  for (int i = 0; i < NUM; i++)
    input[i] = 0;
  printf("testing mixed2 ... ");
  Test(10,MIXED2);
  printf("\n");
  for (int i = 0; i < NUM; i++){
    if (input[i] != 1){
      printf("mixed2 code error!\n");
      return -1;
    }
  }
  printf("ok");
  return 0;
  */

  // warm the CPU.
  int sum = 0;
  for (int i = 0; i < NUM * 10; i++)
    sum += i;

  printf("%10s", "thread_num");
  printf("%10s",  method_name[MUTEX]);
  printf("%10s",  method_name[SPIN1]);
  printf("%10s",  method_name[SPIN2]);
  printf("%10s",  method_name[CAS]);
  printf("%10s",  method_name[MIXED1]);
  printf("%10s",  method_name[MIXED2]);
  printf("\n");

  for (int t = 1; t < 11; t++){
    printf("%10d",t);
    Test(t,MUTEX);
    Test(t,SPIN1);
    Test(t,SPIN2);
    if (t <= 5) Test(t,CAS); else printf("%10s","Na");
    Test(t,MIXED1);
    Test(t,MIXED2);
    printf("\n");
  }

  return 0;
}

