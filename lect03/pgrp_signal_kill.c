#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<sys/wait.h>

int main(int argc, char *argv[])
{
	int child, pid, status;
	pid = fork();
	if (pid == 0) { //자식 프로세스
		printf("PGRP of child = %d\n", getpgrp());

		while(1) {
			printf("Child is waiting...\n");
			sleep(1);
		}
	}
	else {
		printf("PGRP of parent = %d\n", getpgrp());
			
		sleep(5);	
		kill(-getpid(), 9); // Kill(-getpid(), 9)를 해보면?

		printf("[%d] Child %d is terminated \n", getpid(), pid);
		printf("\t...with status %d\n", status>>8);

		printf("Parent is sleeping...\n");
		sleep(5);
	}
	return 0;
}
/*
PGRP of parent = 1554
PGRP of child = 1554
Child is waiting...
Child is waiting...
Child is waiting...
Child is waiting...
Child is waiting...
Killed
*/
