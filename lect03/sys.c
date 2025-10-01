#include<sys/wait.h>
#include<stdio.h>

int main()
{
	int status;
	if ((status = system("date")) < 0)
		perror("system() error");
	printf("Return code = %d\n", WEXITSTATUS(status));

	if ((status = system("hello")) < 0)
		perror("system() error");
	printf("Return code = %d\n", WEXITSTATUS(status));

	if ((status = system("who; exit 44")) < 0)
		perror("system() error");
	printf("Return code = %d\n", WEXITSTATUS(status));

	return 0;
}

/*
Thu Oct  2 02:08:59 KST 2025
Return code = 0
sh: 1: hello: not found
Return code = 127
thgml89567 pts/1        2025-10-01 09:22
Return code = 44
*/
