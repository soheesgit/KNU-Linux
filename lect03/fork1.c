#include <stdio.h>
#include <unistd.h>

int main()
{
	int pid;
	printf("[%d] Before fork() \n", getpid());
	pid = fork();
	printf("[%d] Process : return value = %d\n", getpid(), pid);
}
/* 
 * 실행 결과 :
 * [869] Before fork()
 * [869] Process : return value = 870
 * [870] Process : return value = 0
 */
