#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main()
{
	int pid;
	pid = fork();
	if (pid == 0) {
		printf("[Child] : Hello, pid=%d\n", getpid());
	}
	else {
		printf("[Parent] : Hello, pid = %d\n", getpid());
	}
}

/* 실행 결과:
[Parent] : Hello, pid = 902
[Child] : Hello, pid=903
*/
