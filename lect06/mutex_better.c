#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

pthread_mutex_t mtx;
int acc = 0;

void *TaskCode(void *argument) {
   int tid;
   tid = *((int*) argument);

   int partial_acc = 0;
   for (int i = 0; i < 1000000; i++) {
	partial_acc = partial_acc+1;
   }

   // 제일 좋은 코드!! 동기화횟수 줄여주고 한번에 연동
	pthread_mutex_lock(&mtx);
	acc += partial_acc;
	pthread_mutex_unlock(&mtx);
   

   return NULL;
}

int main(int argc, char *argv[]) {
   pthread_t threads[4];
   int args[4];
   int i;

   /* create all threads */
   for (i = 0; i < 4; i++) {
      args[i] = i;
      pthread_create(&threads[i], NULL, TaskCode, (void *)&args[i]);
   }

   /* wait for all threads to complete */
   for (i = 0; i < 4; i++)
      pthread_join(threads[i], NULL);

   printf("%d\n", acc);
   return 0;
}
