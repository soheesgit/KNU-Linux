#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#define _USE_MATH_DEFINES
#define N 4

void sinx_taylor(int num_elements, int terms, double* x, double* result) {
    for(int i=0; i<num_elements; i++) {
        double value = x[i];
        double numer = x[i] * x[i] * x[i];
        double denom = 6.; // 3!
        int sign = -1;
        for(int j=1; j<=terms; j++) {
            value += (double)sign * numer / denom;
            numer *= x[i] * x[i];
            denom *= (2.*(double)j+2.) * (2.*(double)j+3.);
            sign *= -1;
        }
        result[i] = value;
    }
}


int main() {
    double x[N] = {0, M_PI/6.0, M_PI/3.0, 0.134};
    double result[N];
    int fd[2];

        // 각 x[i] 마다 자식 프로세스 하나 생성
    for (int i = 0; i < N; i++) {
        pid_t pid = fork();

        // 자식 프로세스
        if (pid == 0) {
            close(fd[0]); // 읽기 닫기
            double val = sin_taylor(x[i], 3);
            write(fd[1], &val, sizeof(val));
            close(fd[1]);
            exit(0);
        }
    }

    // 부모 프로세스
    close(fd[1]); // 쓰기 닫기

    // 자식들이 보낸 결과 읽기
    for (int i = 0; i < N; i++) {
        read(fd[0], &result[i], sizeof(double));
    }
    close(fd[0]);

    // 모든 자식 종료 대기
    for (int i = 0; i < N; i++) {
        wait(NULL);
    }

    // 결과 출력
    for (int i = 0; i < N; i++) {
        printf("sin(%.2f) by Taylor = %.8f | 실제 sin(%.2f) = %.8f\n",
               x[i], result[i], x[i], sin(x[i]));
    }

    return 0;
}
