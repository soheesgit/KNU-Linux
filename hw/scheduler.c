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
#define MAX_TIME_QUANTUM 10
#define MAX_CPU_BURST 10
#define MAX_IO_TIME 5

// Process states
enum State {
    READY,
    RUNNING,
    SLEEP,
    DONE
};

// PCB (Process Control Block) structure
typedef struct {
    pid_t pid;
    int remaining_quantum;
    int cpu_burst;          // 부모가 관리하는 CPU burst
    int io_wait_time;
    enum State state;
    int wait_time;
    int start_time;
    int completion_time;
} PCB;

// Global variables
PCB pcb_table[MAX_PROCESSES];
const int num_processes = 10;  // 프로세스 수 10개 고정
int current_process = -1;
int last_scheduled = -1;  // 마지막으로 스케줄된 프로세스 (라운드 로빈용)
int timer_count = 0;
volatile int completed_processes = 0;  // volatile 추가
int time_quantum = 3;  // 기본값
int current_time = 0;

// 간트 차트용 배열 (각 시간, 각 프로세스의 상태 기록)
#define MAX_TIME 500
int gantt_chart[MAX_PROCESSES][MAX_TIME];  // 0=none, 1=READY, 2=RUNNING, 3=SLEEP

// Signal handlers
void parent_timer_handler(int sig);
void parent_io_handler(int sig);
void parent_child_handler(int sig);  // SIGCHLD 핸들러 추가
void child_signal_handler(int sig);

// Function prototypes
void initialize_pcb(int index, pid_t pid);
int find_next_ready_process();
void schedule_next_process();
void update_wait_times();
void print_status();
void calculate_statistics();
void reset_all_quantum();
int find_process_by_pid(pid_t pid);
void print_gantt_chart();  // 간트 차트 출력 함수

// 자식 프로세스용 전역 변수
volatile int child_cpu_burst = 0;
volatile int child_should_exit = 0;
volatile int child_io_request = 0;

int main(int argc, char *argv[]) {
    pid_t child_pids[MAX_PROCESSES];
    char input[100];
    
    // 타임 퀀텀 입력 받기
    printf("타임 퀀텀을 입력해주세요 (기본값: 3, 최대: %d): ", MAX_TIME_QUANTUM);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) != NULL && input[0] != '\n') {
        int temp = atoi(input);
        if (temp > 0 && temp <= MAX_TIME_QUANTUM) {
            time_quantum = temp;
        } else {
            printf("잘못된 값입니다. 기본값 3을 사용합니다.\n");
            time_quantum = 3;
        }
    } else {
        printf("기본값 3을 사용합니다.\n");
        time_quantum = 3;
    }
    
    printf("\n=== OS 스케줄링 시뮬레이션 ===\n");
    printf("프로세스 수: %d\n", num_processes);
    printf("타임 퀀텀: %d\n", time_quantum);
    printf("===============================\n\n");
    
    // 간트 차트 배열 초기화
    for (int p = 0; p < MAX_PROCESSES; p++) {
        for (int t = 0; t < MAX_TIME; t++) {
            gantt_chart[p][t] = 0;
        }
    }
    
    // Seed random number generator
    srand(time(NULL));
    
    // Create child processes
    for (int i = 0; i < num_processes; i++) {
        pid_t pid = fork();
        
        if (pid == 0) {
            // Child process code
            // 자식별로 다른 시드 사용
            srand(time(NULL) ^ getpid());
            
            signal(SIGUSR1, child_signal_handler);
            
            // Initialize CPU burst (1-10)
            child_cpu_burst = (rand() % MAX_CPU_BURST) + 1;
            
            // Wait for scheduling signal
            while (!child_should_exit) {
                pause();  // Wait for signal
                
                if (child_should_exit) {
                    break;
                }
                
                if (child_io_request) {
                    // I/O 요청 시그널을 부모에게 보냄
                    kill(getppid(), SIGUSR2);
                    child_io_request = 0;
                    child_cpu_burst = (rand() % MAX_CPU_BURST) + 1;
                }
            }
            
            exit(0);
        } else if (pid > 0) {
            // Parent process
            child_pids[i] = pid;
            initialize_pcb(i, pid);
            printf("[프로세스 %d] CPU 버스트 %d로 생성됨\n", i, pcb_table[i].cpu_burst);
        } else {
            perror("Fork 실패");
            exit(1);
        }
    }
    
    // Parent process continues here
    // Set up signal handlers
    struct sigaction sa_timer, sa_io, sa_child;
    
    // Timer handler
    sa_timer.sa_handler = parent_timer_handler;
    sigemptyset(&sa_timer.sa_mask);
    sa_timer.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa_timer, NULL);
    
    // I/O handler
    sa_io.sa_handler = parent_io_handler;
    sigemptyset(&sa_io.sa_mask);
    sa_io.sa_flags = SA_RESTART;
    sigaction(SIGUSR2, &sa_io, NULL);
    
    // Child termination handler
    sa_child.sa_handler = parent_child_handler;
    sigemptyset(&sa_child.sa_mask);
    sa_child.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &sa_child, NULL);
    
    // Set up timer (100ms intervals)
    struct itimerval timer;
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 100000;  // 100ms
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 100000;  // 100ms
    
    // 잠시 대기하여 자식 프로세스들이 초기화되도록 함
    usleep(50000);
    
    // Start scheduling
    schedule_next_process();
    
    // Start timer
    setitimer(ITIMER_REAL, &timer, NULL);
    
    // Wait for all children to complete
    while (completed_processes < num_processes) {
        pause();
    }
    
    // Stop timer
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
    
    // 간트 차트 출력
    print_gantt_chart();
    
    // Calculate and print statistics
    calculate_statistics();
    
    return 0;
}

void initialize_pcb(int index, pid_t pid) {
    pcb_table[index].pid = pid;
    pcb_table[index].remaining_quantum = time_quantum;
    pcb_table[index].cpu_burst = (rand() % MAX_CPU_BURST) + 1;
    pcb_table[index].io_wait_time = 0;
    pcb_table[index].state = READY;
    pcb_table[index].wait_time = 0;
    pcb_table[index].start_time = current_time;  // 도착 시간 (프로세스 생성 시점)
    pcb_table[index].completion_time = -1;
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
        if (index != -1 && pcb_table[index].state != DONE) {
            pcb_table[index].state = DONE;
            pcb_table[index].completion_time = current_time;
            completed_processes++;
            printf("[시간:%d][프로세스 %d] 종료됨. 완료: %d/%d\n", 
                   current_time, index, completed_processes, num_processes);
            
            // 현재 실행 중인 프로세스가 종료되었으면 다음 프로세스 스케줄
            if (current_process == index) {
                current_process = -1;
                schedule_next_process();
            }
        }
    }
}

void parent_timer_handler(int sig) {
    current_time++;
    update_wait_times();
    
    // 10초마다 구분선 출력
    if (current_time % 10 == 0) {
        printf("────────────────────────────── [%d초] ──────────────────────────────\n", current_time);
    }
    
    // Check I/O wait times first
    for (int i = 0; i < num_processes; i++) {
        if (pcb_table[i].state == SLEEP) {
            pcb_table[i].io_wait_time--;
            if (pcb_table[i].io_wait_time <= 0) {
                printf("[시간:%d][프로세스 %d] I/O 완료, READY로 이동\n", current_time, i);
                pcb_table[i].state = READY;
                pcb_table[i].remaining_quantum = time_quantum;
            }
        }
    }
    
    // 간트 차트에 모든 프로세스 상태 기록 (Idle 상태일 때도 기록)
    if (current_time < MAX_TIME) {
        for (int p = 0; p < num_processes; p++) {
            switch (pcb_table[p].state) {
                case READY:   gantt_chart[p][current_time] = 1; break;
                case RUNNING: gantt_chart[p][current_time] = 2; break;
                case SLEEP:   gantt_chart[p][current_time] = 3; break;
                default:      gantt_chart[p][current_time] = 0; break;
            }
        }
    }
    
    if (current_process != -1) {
        PCB *current_pcb = &pcb_table[current_process];
        
        if (current_pcb->state == RUNNING) {
            // Send signal to child to execute one CPU burst
            kill(current_pcb->pid, SIGUSR1);
            
            // 부모측에서도 CPU burst 감소
            current_pcb->cpu_burst--;
            
            // Decrement time quantum
            current_pcb->remaining_quantum--;
            
            // CPU burst가 0이 되면 프로세스 종료 또는 I/O
            if (current_pcb->cpu_burst <= 0) {
                if (rand() % 2 == 0) {
                    // 프로세스 종료 요청
                    printf("[시간:%d][프로세스 %d] CPU 버스트 완료, 종료 중\n", current_time, current_process);
                    // SIGTERM을 보내서 자식 종료 유도
                    kill(current_pcb->pid, SIGTERM);
                } else {
                    // I/O 요청
                    printf("[시간:%d][프로세스 %d] CPU 버스트 완료, I/O 요청\n", current_time, current_process);
                    current_pcb->io_wait_time = (rand() % MAX_IO_TIME) + 1;
                    current_pcb->state = SLEEP;
                    current_pcb->cpu_burst = (rand() % MAX_CPU_BURST) + 1;
                    schedule_next_process();
                }
            }
            // Check if time quantum expired
            else if (current_pcb->remaining_quantum <= 0) {
                printf("[시간:%d][프로세스 %d] 타임 퀀텀 만료\n", current_time, current_process);
                current_pcb->state = READY;
                current_pcb->remaining_quantum = time_quantum;
                schedule_next_process();
            }
        }
    } else {
        // 현재 실행 중인 프로세스가 없으면 스케줄링 시도
        schedule_next_process();
    }
}

void parent_io_handler(int sig) {
    // 자식에서 I/O 요청이 왔을 때 처리
    if (current_process != -1) {
        PCB *current_pcb = &pcb_table[current_process];
        if (current_pcb->state == RUNNING) {
            current_pcb->io_wait_time = (rand() % MAX_IO_TIME) + 1;
            current_pcb->state = SLEEP;
            current_pcb->cpu_burst = (rand() % MAX_CPU_BURST) + 1;
            printf("[시간:%d][프로세스 %d] I/O 요청, 대기 시간: %d\n", 
                   current_time, current_process, current_pcb->io_wait_time);
            schedule_next_process();
        }
    }
}

void child_signal_handler(int sig) {
    if (sig == SIGUSR1) {
        // CPU burst 감소
        child_cpu_burst--;
        
        if (child_cpu_burst <= 0) {
            // 랜덤하게 종료 또는 I/O
            if (rand() % 2 == 0) {
                child_should_exit = 1;
            } else {
                child_io_request = 1;
            }
        }
    } else if (sig == SIGTERM) {
        child_should_exit = 1;
    }
}

int find_next_ready_process() {
    // Round-robin: find next ready process after last_scheduled
    int start = (last_scheduled + 1) % num_processes;
    
    for (int i = 0; i < num_processes; i++) {
        int index = (start + i) % num_processes;
        if (pcb_table[index].state == READY) {
            return index;
        }
    }
    
    return -1;  // No ready process found
}

void schedule_next_process() {
    int next = find_next_ready_process();
    
    if (next != -1) {
        if (current_process != -1 && pcb_table[current_process].state == RUNNING) {
            pcb_table[current_process].state = READY;
        }
        
        current_process = next;
        last_scheduled = next;  // 라운드 로빈을 위해 마지막 스케줄 기록
        pcb_table[current_process].state = RUNNING;
        
        printf("[시간:%d][프로세스 %d] 스케줄링\n", 
               current_time, current_process);
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
    printf("프로세스\t상태\t\t퀀텀\tCPU 버스트\tI/O 대기\t대기 시간\n");
    printf("--------------------------------------------------------------------\n");
    
    for (int i = 0; i < num_processes; i++) {
        const char *state_str;
        switch (pcb_table[i].state) {
            case READY: state_str = "READY"; break;
            case RUNNING: state_str = "RUNNING"; break;
            case SLEEP: state_str = "SLEEP"; break;
            case DONE: state_str = "DONE"; break;
            default: state_str = "UNKNOWN";
        }
        
        printf("%d\t%s\t\t%d\t%d\t\t%d\t\t%d\n",
               i, state_str, 
               pcb_table[i].remaining_quantum,
               pcb_table[i].cpu_burst,
               pcb_table[i].io_wait_time,
               pcb_table[i].wait_time);
    }
    printf("완료: %d/%d\n\n", completed_processes, num_processes);
}

void calculate_statistics() {
    printf("\n=== 최종 통계 ===\n");
    printf("사용된 타임 퀀텀: %d\n", time_quantum);
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
    
    printf("=================\n");
}

void reset_all_quantum() {
    for (int i = 0; i < num_processes; i++) {
        if (pcb_table[i].state != DONE) {
            pcb_table[i].remaining_quantum = time_quantum;
        }
    }
}

void print_gantt_chart() {
    printf("\n=== 간트 차트 ===\n\n");
    
    int total_time = current_time;
    if (total_time > MAX_TIME) total_time = MAX_TIME;
    if (total_time > 80) total_time = 80;  // 화면에 맞게 최대 80칸
    
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
                default: printf(" "); break;  // DONE or not started
            }
        }
        printf("\n");
    }
    
    // 범례
    printf("\n범례:  █ = RUNNING   ░ = SLEEP   · = READY\n");
}