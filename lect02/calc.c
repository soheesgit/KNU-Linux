#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("사용법: %s <숫자1> <연산자> <숫자2>\n", argv[0]);
        return 1;
    }

    int a = atoi(argv[1]);
    char op = argv[2][0];
    int b = atoi(argv[3]);
    int result;

    switch (op) {
        case '+': result = a + b; break;
        case '-': result = a - b; break;
        case '*': result = a * b; break;
        case '/': 
            if (b == 0) {
                printf("0으로 나눌 수 없습니다!\n");
                return 1;
            }
            result = a / b; 
            break;
        default:
            printf("알 수 없는 연산자: %c\n", op);
            return 1;
    }

    printf("%d\n", result);
    return 0;
}
