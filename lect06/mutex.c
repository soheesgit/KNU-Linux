#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

pthread_mutex_t mtx;
int acc = 0;

void *TaskCode(void *argument) {
   int tid;
   tid = *((int*) argument);

	// mutex lock, unlock을 통해 값이 실행할때마다 바뀌는 랜덤한 값이 아니라 4000000이 나오도록 고정

// 포문의 전에 쓰레드 lock을 정의하고 끝나고 unlock 정의하는건 더 안좋은 코드다
// 그러면 병렬처리를 안함!
//
// 이건 그때그때 코드 요청해야해서 좋진 않은 코드
   for (int i = 0; i < 1000000; i++) {
   
	pthread_mutex_lock(&mtx);
	acc = acc + 1;
	pthread_mutex_unlock(&mtx);
   }

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
