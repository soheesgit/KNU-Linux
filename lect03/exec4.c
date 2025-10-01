#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	int child, pid, status;
	pid = fork();
	if (pid == 0) { //자식 프로세스
		execvp(argv[1], &argv[1]);
		fprintf(stderr, "%s: Error in exec\n", argv[1]);
	} else { //부모 프로세스
		child = wait(&status);
		printf("[%d] Child %d is terminated \n", getpid(), pid);
		printf("\t ...with status %d \n", status>>8);
	}
	return 0;
}
/* 실행결과:
[1396] Child 1397 is terminated
         ...with status 0
	 */
