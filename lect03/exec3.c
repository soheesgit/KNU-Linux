#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
/* 자식 프로세스를 생성하여 echo 명령어를 실행한다.*/
int main()
{
	if (fork() == 0) {
		char *argv[3];
		argv[0] = "echo"; 
		argv[1] = "hello"; 
		argv[2] = NULL;
		execv("/bin/echo", argv); // ppt는 ppt에는 execv("/bin/echo", "echo", "hello", NULL) 인데 실행안됨`
	}
	printf("End of parent process\n");
	return 0;
}
