#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

#define MAX_PROCESSES 50
#define MAX_CPU_BURST 10
#define MAX_IO_TIME 5

// 프로세스 상태
enum State {
    READY,
    RUNNING,
    SLEEP,
    DONE
};

// PCB (프로세스 제어 블록) 구조체
typedef struct {
    pid_t pid;
    int cpu_burst;          // 부모가 관리하는 CPU 버스트
    int io_wait_time;
    enum State state;
    int wait_time;
    int start_time;
    int completion_time;
    int ready_queue_time;   // FIFO: Ready Queue에 진입한 시간
} PCB;

// 전역 변수
PCB pcb_table[MAX_PROCESSES];
const int num_processes = 10;  // 프로세스 수 10개 고정
int current_process = -1;
int timer_count = 0;
volatile int completed_processes = 0;
int current_time = 0;

// 간트 차트용 배열 (각 시간, 각 프로세스의 상태 기록)
#define MAX_TIME 500
int gantt_chart[MAX_PROCESSES][MAX_TIME];  // 0=없음, 1=READY, 2=RUNNING, 3=SLEEP

// 시그널 마스크 (모든 핸들러에서 사용)
sigset_t block_mask;

// 시그널 핸들러
void parent_timer_handler(int sig);
void parent_child_handler(int sig);
void child_signal_handler(int sig);

// 함수 원형
void initialize_pcb(int index, pid_t pid, int cpu_burst);
int find_next_ready_process();
void schedule_next_process();
void update_wait_times();
void print_status();
void calculate_statistics();
int find_process_by_pid(pid_t pid);
void print_gantt_chart();

// 자식 프로세스용 전역 변수
volatile int child_should_exit = 0;

int main(int argc, char *argv[]) {
    pid_t child_pids[MAX_PROCESSES];
    int cpu_bursts[MAX_PROCESSES];
    char input[100];
    
    printf("\n=== OS 스케줄링 시뮬레이션 (비선점형 FIFO) ===\n");
    printf("프로세스 수: %d\n", num_processes);
    printf("스케줄링 방식: 비선점형 FIFO (Ready Queue 진입 순서 기준)\n");
    printf("================================================\n\n");
    
    // 각 프로세스의 CPU 버스트 입력받기
    printf("각 프로세스의 CPU 버스트 값을 입력하세요 (1-%d):\n", MAX_CPU_BURST);
    for (int i = 0; i < num_processes; i++) {
        while (1) {
            printf("  프로세스 %d의 CPU 버스트: ", i);
            fflush(stdout);
            if (fgets(input, sizeof(input), stdin) != NULL && input[0] != '\n') {
                int burst = atoi(input);
                if (burst >= 1 && burst <= MAX_CPU_BURST) {
                    cpu_bursts[i] = burst;
                    break;
                } else {
                    printf("    잘못된 값입니다. 1-%d 사이의 값을 입력하세요.\n", MAX_CPU_BURST);
                }
            } else {
                printf("    값을 입력해주세요.\n");
            }
        }
    }
    printf("\n");
    
    // 간트 차트 배열 초기화
    for (int p = 0; p < MAX_PROCESSES; p++) {
        for (int t = 0; t < MAX_TIME; t++) {
            gantt_chart[p][t] = 0;
        }
    }
    
    // 시그널 마스크 설정 (핸들러 실행 중 블록할 시그널)
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGALRM);
    sigaddset(&block_mask, SIGCHLD);
    
    // 난수 생성기 시드 설정
    srand(time(NULL));
    
    // 자식 프로세스 생성
    for (int i = 0; i < num_processes; i++) {
        // 입력받은 CPU 버스트 값 사용
        int initial_burst = cpu_bursts[i];
        
        pid_t pid = fork();
        
        if (pid == 0) {
            // 자식 프로세스 코드 - 단순히 시그널 대기만 함
            signal(SIGUSR1, child_signal_handler);
            signal(SIGTERM, child_signal_handler);
            
            // 스케줄링 시그널 대기
            while (!child_should_exit) {
                pause();  // 시그널 대기
            }
            
            exit(0);
        } else if (pid > 0) {
            // 부모 프로세스
            child_pids[i] = pid;
            initialize_pcb(i, pid, initial_burst);
            printf("[프로세스 %d] CPU 버스트 %d로 생성됨 (Ready Queue 진입: 시간 %d)\n", 
                   i, pcb_table[i].cpu_burst, pcb_table[i].ready_queue_time);
        } else {
            perror("Fork 실패");
            exit(1);
        }
    }
    
    // 부모 프로세스 계속 실행
    // 시그널 핸들러 설정
    struct sigaction sa_timer, sa_child;
    
    // 타이머 핸들러 - 다른 시그널 블록
    sa_timer.sa_handler = parent_timer_handler;
    sa_timer.sa_mask = block_mask;
    sa_timer.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa_timer, NULL);
    
    // 자식 종료 핸들러 - 다른 시그널 블록
    sa_child.sa_handler = parent_child_handler;
    sa_child.sa_mask = block_mask;
    sa_child.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa_child, NULL);
    
    // 타이머 설정 (100ms 간격)
    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 100000;  // 100ms
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 100000;  // 100ms
    
    // 잠시 대기하여 자식 프로세스들이 초기화되도록 함
    usleep(50000);
    
    // 스케줄링 시작
    schedule_next_process();
    
    // 타이머 시작
    setitimer(ITIMER_REAL, &timer, NULL);
    
    // 모든 자식 프로세스 완료 대기
    while (completed_processes < num_processes) {
        pause();
    }
    
    // 타이머 정지
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
    
    // 간트 차트 출력
    print_gantt_chart();
    
    // 통계 계산 및 출력
    calculate_statistics();
    
    return 0;
}

void initialize_pcb(int index, pid_t pid, int cpu_burst) {
    pcb_table[index].pid = pid;
    pcb_table[index].cpu_burst = cpu_burst;  // 전달받은 값 사용
    pcb_table[index].io_wait_time = 0;
    pcb_table[index].state = READY;
    pcb_table[index].wait_time = 0;
    pcb_table[index].start_time = current_time;  // 도착 시간 (프로세스 생성 시점)
    pcb_table[index].completion_time = -1;
    pcb_table[index].ready_queue_time = current_time;  // 처음 생성 시 Ready Queue 진입 시간
}

int find_process_by_pid(pid_t pid) {
    for (int i = 0; i < num_processes; i++) {
        if (pcb_table[i].pid == pid) {
            return i;
        }
    }
    return -1;
}

void parent_child_handler(int sig) {
    int status;
    pid_t pid;
    
    // 모든 종료된 자식 프로세스 처리
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int index = find_process_by_pid(pid);
        if (index != -1) {
            // 아직 DONE 처리 안 된 경우만 카운트
            if (pcb_table[index].state != DONE) {
                pcb_table[index].state = DONE;
                pcb_table[index].completion_time = current_time;
                completed_processes++;
                printf("[시간:%d][프로세스 %d] 종료됨. 완료: %d/%d\n", 
                       current_time, index, completed_processes, num_processes);
            }
            
            // 현재 실행 중인 프로세스가 종료되었으면 다음 프로세스 스케줄
            if (current_process == index) {
                current_process = -1;
            }
            // 다음 프로세스 스케줄 (SIGCHLD에서)
            if (current_process == -1) {
                schedule_next_process();
            }
        }
    }
}

void parent_timer_handler(int sig) {
    current_time++;
    update_wait_times();
    
    int executed_this_tick = -1;  // 이번 틱에 실행한 프로세스 기억
    
    // 10초마다 구분선 출력
    if (current_time % 10 == 0) {
        printf("────────────────────────────── [%d초] ──────────────────────────────\n", current_time);
    }
    
    // I/O 대기 시간 먼저 확인
    for (int i = 0; i < num_processes; i++) {
        if (pcb_table[i].state == SLEEP) {
            pcb_table[i].io_wait_time--;
            if (pcb_table[i].io_wait_time <= 0) {
                printf("[시간:%d][프로세스 %d] I/O 완료, READY로 이동 (Ready Queue 진입: 시간 %d)\n", 
                       current_time, i, current_time);
                pcb_table[i].state = READY;
                pcb_table[i].ready_queue_time = current_time;  // Ready Queue 재진입 시간 갱신
            }
        }
    }
    
    if (current_process != -1) {
        PCB *current_pcb = &pcb_table[current_process];
        
        if (current_pcb->state == RUNNING) {
            // 이번 틱에 실행한 프로세스 기억 (간트 차트용)
            executed_this_tick = current_process;
            
            // 자식에게 시그널 보내서 CPU 버스트 실행
            kill(current_pcb->pid, SIGUSR1);
            
            // 부모측에서 CPU 버스트 감소
            current_pcb->cpu_burst--;
            
            // CPU 버스트가 0이 되면 프로세스 종료 또는 I/O
            // 비선점형 FIFO: CPU 버스트가 완료될 때까지 계속 실행
            if (current_pcb->cpu_burst <= 0) {
                if (rand() % 2 == 0) {
                    // 프로세스 종료 요청
                    printf("[시간:%d][프로세스 %d] CPU 버스트 완료, 종료 중\n", current_time, current_process);
                    // 바로 DONE 상태로 변경 (간트 차트에 READY로 기록되는 것 방지)
                    current_pcb->state = DONE;
                    current_pcb->completion_time = current_time;
                    completed_processes++;
                    printf("[시간:%d][프로세스 %d] 종료됨. 완료: %d/%d\n", 
                           current_time, current_process, completed_processes, num_processes);
                    // SIGTERM을 보내서 자식 종료 유도
                    kill(current_pcb->pid, SIGTERM);
                    current_process = -1;
                    schedule_next_process();
                } else {
                    // I/O 요청
                    int io_time = (rand() % MAX_IO_TIME) + 1;
                    printf("[시간:%d][프로세스 %d] CPU 버스트 완료, I/O 요청 (대기: %d)\n", 
                           current_time, current_process, io_time);
                    current_pcb->io_wait_time = io_time;
                    current_pcb->state = SLEEP;
                    current_pcb->cpu_burst = (rand() % MAX_CPU_BURST) + 1;
                    current_process = -1;
                    schedule_next_process();
                }
            }
            // 비선점형 FIFO: 타임 퀀텀 만료 체크 없음 - CPU 버스트가 끝날 때까지 계속 실행
        }
    } else {
        // 현재 실행 중인 프로세스가 없으면 스케줄링 시도
        schedule_next_process();
    }
    
    // 간트 차트에 모든 프로세스 상태 기록 (모든 상태 변경 후에 기록)
    if (current_time < MAX_TIME) {
        for (int p = 0; p < num_processes; p++) {
            // 이번 틱에 실행한 프로세스는 상태와 관계없이 RUNNING으로 기록
            if (p == executed_this_tick) {
                gantt_chart[p][current_time] = 2;  // RUNNING
            } else {
                switch (pcb_table[p].state) {
                    case READY:   gantt_chart[p][current_time] = 1; break;
                    case RUNNING: gantt_chart[p][current_time] = 2; break;
                    case SLEEP:   gantt_chart[p][current_time] = 3; break;
                    default:      gantt_chart[p][current_time] = 0; break;
                }
            }
        }
    }
}

void child_signal_handler(int sig) {
    if (sig == SIGTERM) {
        child_should_exit = 1;
    }
    // SIGUSR1은 단순히 "실행 중"임을 나타내는 용도로만 사용
}

// 비선점형 FIFO: Ready Queue에 가장 먼저 진입한 프로세스 찾기
int find_next_ready_process() {
    int earliest_ready = -1;
    int earliest_time = MAX_TIME + 1;
    
    for (int i = 0; i < num_processes; i++) {
        if (pcb_table[i].state == READY) {
            // Ready Queue 진입 시간이 더 빠른(작은) 프로세스 선택
            // 동일 시간인 경우 인덱스가 작은 프로세스 선택 (생성 순서)
            if (pcb_table[i].ready_queue_time < earliest_time ||
                (pcb_table[i].ready_queue_time == earliest_time && i < earliest_ready)) {
                earliest_time = pcb_table[i].ready_queue_time;
                earliest_ready = i;
            }
        }
    }
    
    return earliest_ready;  // READY 프로세스 없으면 -1 반환
}

void schedule_next_process() {
    int next = find_next_ready_process();
    
    if (next != -1) {
        current_process = next;
        pcb_table[current_process].state = RUNNING;
        
        printf("[시간:%d][프로세스 %d] 스케줄링 (FIFO - Ready Queue 진입: 시간 %d)\n", 
               current_time, current_process, pcb_table[current_process].ready_queue_time);
    } else {
        current_process = -1;
    }
}

void update_wait_times() {
    for (int i = 0; i < num_processes; i++) {
        if (pcb_table[i].state == READY) {
            pcb_table[i].wait_time++;
        }
    }
}

void print_status() {
    printf("\n--- 시스템 상태 (시간: %d) ---\n", current_time);
    printf("프로세스\t상태\t\tCPU 버스트\tI/O 대기\t대기 시간\tReady진입\n");
    printf("------------------------------------------------------------------------\n");
    
    for (int i = 0; i < num_processes; i++) {
        const char *state_str;
        switch (pcb_table[i].state) {
            case READY: state_str = "READY"; break;
            case RUNNING: state_str = "RUNNING"; break;
            case SLEEP: state_str = "SLEEP"; break;
            case DONE: state_str = "DONE"; break;
            default: state_str = "UNKNOWN";
        }
        
        printf("%d\t%s\t\t%d\t\t%d\t\t%d\t\t%d\n",
               i, state_str, 
               pcb_table[i].cpu_burst,
               pcb_table[i].io_wait_time,
               pcb_table[i].wait_time,
               pcb_table[i].ready_queue_time);
    }
    printf("완료: %d/%d\n\n", completed_processes, num_processes);
}

void calculate_statistics() {
    printf("\n=== 최종 통계 (비선점형 FIFO) ===\n");
    printf("스케줄링 방식: 비선점형 FIFO (Ready Queue 진입 순서 기준)\n");
    printf("총 시뮬레이션 시간: %d\n", current_time);
    
    int total_wait_time = 0;
    int total_turnaround_time = 0;
    int process_count = 0;
    
    for (int i = 0; i < num_processes; i++) {
        if (pcb_table[i].state == DONE && pcb_table[i].completion_time != -1) {
            int turnaround = pcb_table[i].completion_time - pcb_table[i].start_time;
            total_wait_time += pcb_table[i].wait_time;
            total_turnaround_time += turnaround;
            process_count++;
            printf("프로세스 %d - 대기 시간: %d, 턴어라운드: %d\n", 
                   i, pcb_table[i].wait_time, turnaround);
        }
    }
    
    if (process_count > 0) {
        double avg_wait_time = (double)total_wait_time / process_count;
        double avg_turnaround = (double)total_turnaround_time / process_count;
        printf("\n평균 대기 시간: %.2f time units\n", avg_wait_time);
        printf("평균 턴어라운드 시간: %.2f time units\n", avg_turnaround);
    }
    
    printf("=================================\n");
}

void print_gantt_chart() {
    printf("\n=== 간트 차트 (비선점형 FIFO) ===\n\n");
    
    int total_time = current_time;
    if (total_time > MAX_TIME) total_time = MAX_TIME;
    if (total_time > 150) total_time = 150;  // 화면에 맞게 최대 150칸
    
    // 시간 헤더
    printf("시간: ");
    for (int t = 0; t <= total_time; t += 10) {
        printf("%-10d", t);
    }
    printf("\n");
    
    // 눈금자
    printf("      ");
    for (int t = 1; t <= total_time; t++) {
        if (t % 10 == 0) {
            printf("|");
        } else if (t % 5 == 0) {
            printf("+");
        } else {
            printf("-");
        }
    }
    printf("\n");
    
    // 각 프로세스별 타임라인
    for (int p = 0; p < num_processes; p++) {
        printf("P%-4d ", p);
        for (int t = 1; t <= total_time; t++) {
            switch (gantt_chart[p][t]) {
                case 1:  printf("·"); break;  // READY
                case 2:  printf("█"); break;  // RUNNING
                case 3:  printf("░"); break;  // SLEEP
                default: printf(" "); break;  // DONE 또는 시작 전
            }
        }
        printf("\n");
    }
    
    // 범례
    printf("\n범례:  █ = RUNNING   ░ = SLEEP   · = READY\n");
}