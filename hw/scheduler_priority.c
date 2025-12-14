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
#define MAX_CPU_BURST 50       // CPU 버스트 최대값 증가 (에이징 효과 확인용)
#define MAX_IO_TIME 5

// 우선순위 관련 상수
#define MAX_PRIORITY 10         // 최저 우선순위 (숫자가 클수록 낮은 우선순위)
#define MIN_PRIORITY 0          // 최고 우선순위
#define AGING_INTERVAL 10       // 에이징 간격 (10초마다)
#define AGING_AMOUNT 1          // 에이징 시 우선순위 증가량 (숫자 감소)

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
    int remaining_quantum;
    int cpu_burst;          // 부모가 관리하는 CPU 버스트
    int io_wait_time;
    enum State state;
    int wait_time;
    int start_time;
    int completion_time;
    int priority;           // 현재 우선순위 (0=최고, 숫자가 클수록 낮음)
    int initial_priority;   // 초기 우선순위 (I/O 복귀 시 리셋용)
    int aging_counter;      // 에이징 카운터 (READY 상태 지속 시간)
    int reached_top;        // 최고 우선순위 도달 여부 (출력용)
} PCB;

// 전역 변수
PCB pcb_table[MAX_PROCESSES];
const int num_processes = 5;  // 프로세스 수 5개
int current_process = -1;
int last_scheduled = -1;  // 같은 우선순위 내 라운드 로빈용
int timer_count = 0;
volatile int completed_processes = 0;
int time_quantum = 3;  // 기본값
int current_time = 0;

// 간트 차트용 배열 (각 시간, 각 프로세스의 상태 기록)
#define MAX_TIME 1000
int gantt_chart[MAX_PROCESSES][MAX_TIME];  // 0=없음, 1=READY, 2=RUNNING, 3=SLEEP

// 시그널 마스크 (모든 핸들러에서 사용)
sigset_t block_mask;

// 시그널 핸들러
void parent_timer_handler(int sig);
void parent_child_handler(int sig);
void child_signal_handler(int sig);

// 함수 원형
void initialize_pcb(int index, pid_t pid, int cpu_burst, int priority);
int find_next_ready_process();
void schedule_next_process();
void update_wait_times();
void apply_aging();
void print_status();
void calculate_statistics();
void reset_all_quantum();
int find_process_by_pid(pid_t pid);
void print_gantt_chart();

// 자식 프로세스용 전역 변수
volatile int child_should_exit = 0;

int main(int argc, char *argv[]) {
    pid_t child_pids[MAX_PROCESSES];
    
    // 타임 퀀텀 고정
    time_quantum = 3;
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║       우선순위 스케줄링 + 에이징 (Priority Scheduling)         ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║  프로세스 수: %-3d                                              ║\n", num_processes);
    printf("║  타임 퀀텀: %-3d                                                ║\n", time_quantum);
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║  [에이징 설정]                                                 ║\n");
    printf("║  • READY 상태로 %d초 대기 시 우선순위 +%d (숫자↓ = 우선순위↑)  ║\n", AGING_INTERVAL, AGING_AMOUNT);
    printf("║  • 타임퀀텀 만료 시 우선순위 -1 (숫자↑ = 우선순위↓)            ║\n");
    printf("║  • I/O 완료 시 우선순위 +1 (I/O 바운드 프로세스 보상)          ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║  [초기 우선순위]                                               ║\n");
    printf("║  • P0=0(최고) ~ P4=4(최저)                                     ║\n");
    printf("║  • 에이징 없으면 P4는 P0~P3이 끝날 때까지 계속 대기 (기아)     ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    fflush(stdout);  // fork 전에 버퍼 비우기
    
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
        // fork() 전에 CPU 버스트와 우선순위 값 미리 생성
        int initial_burst = (rand() % MAX_CPU_BURST) + 1;
        int initial_priority = i;  // P0=0(최고), P9=9(최저) - 에이징 효과 확인용
        
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
            initialize_pcb(i, pid, initial_burst, initial_priority);
        } else {
            perror("Fork 실패");
            exit(1);
        }
    }
    
    // 부모 프로세스 계속 실행
    
    // 생성된 프로세스 정보 표 출력
    printf("┌──────────┬────────────┬────────────┐\n");
    printf("│ 프로세스 │ CPU 버스트 │  우선순위  │\n");
    printf("├──────────┼────────────┼────────────┤\n");
    for (int i = 0; i < num_processes; i++) {
        printf("│    P%d    │     %2d     │     %2d     │\n", 
               i, pcb_table[i].cpu_burst, pcb_table[i].priority);
    }
    printf("└──────────┴────────────┴────────────┘\n");
    printf("\n[시뮬레이션 시작]\n\n");
    fflush(stdout);
    
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

void initialize_pcb(int index, pid_t pid, int cpu_burst, int priority) {
    pcb_table[index].pid = pid;
    pcb_table[index].remaining_quantum = time_quantum;
    pcb_table[index].cpu_burst = cpu_burst;
    pcb_table[index].io_wait_time = 0;
    pcb_table[index].state = READY;
    pcb_table[index].wait_time = 0;
    pcb_table[index].start_time = current_time;
    pcb_table[index].completion_time = -1;
    pcb_table[index].priority = priority;
    pcb_table[index].initial_priority = priority;
    pcb_table[index].aging_counter = 0;
    pcb_table[index].reached_top = 0;
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
                printf("[종료] P%d 완료 (초기우선순위: %d) - %d/%d\n", 
                       index, pcb_table[index].initial_priority, completed_processes, num_processes);
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
    apply_aging();  // 에이징 적용
    
    // 50초마다 구분선 출력
    if (current_time % 50 == 0) {
        printf("─────────────────────── [%d초 경과] ───────────────────────\n", current_time);
    }
    
    // I/O 대기 시간 먼저 확인
    for (int i = 0; i < num_processes; i++) {
        if (pcb_table[i].state == SLEEP) {
            pcb_table[i].io_wait_time--;
            if (pcb_table[i].io_wait_time <= 0) {
                // I/O 완료 시 우선순위 약간 높임 (I/O 바운드 프로세스 보상)
                pcb_table[i].priority--;
                if (pcb_table[i].priority < MIN_PRIORITY) {
                    pcb_table[i].priority = MIN_PRIORITY;
                }
                pcb_table[i].aging_counter = 0;
                pcb_table[i].state = READY;
                pcb_table[i].remaining_quantum = time_quantum;
            }
        }
    }
    
    if (current_process != -1) {
        PCB *current_pcb = &pcb_table[current_process];
        
        if (current_pcb->state == RUNNING) {
            // 자식에게 시그널 보내서 CPU 버스트 실행
            kill(current_pcb->pid, SIGUSR1);
            
            // 부모측에서 CPU 버스트 감소
            current_pcb->cpu_burst--;
            
            // 타임 퀀텀 감소
            current_pcb->remaining_quantum--;
            
            // CPU 버스트가 0이 되면 프로세스 종료 또는 I/O
            if (current_pcb->cpu_burst <= 0) {
                if (rand() % 2 == 0) {
                    // 프로세스 종료 요청
                    current_pcb->state = READY;
                    kill(current_pcb->pid, SIGTERM);
                    current_process = -1;
                } else {
                    // I/O 요청
                    int io_time = (rand() % MAX_IO_TIME) + 1;
                    current_pcb->io_wait_time = io_time;
                    current_pcb->state = SLEEP;
                    current_pcb->cpu_burst = (rand() % MAX_CPU_BURST) + 1;
                    current_process = -1;
                    schedule_next_process();
                }
            }
            // 타임 퀀텀 만료 확인
            else if (current_pcb->remaining_quantum <= 0) {
                // 타임 퀀텀 만료 시 우선순위 낮춤 (숫자 증가)
                current_pcb->priority++;
                if (current_pcb->priority > MAX_PRIORITY) {
                    current_pcb->priority = MAX_PRIORITY;
                }
                current_pcb->state = READY;
                current_pcb->remaining_quantum = time_quantum;
                current_process = -1;
                schedule_next_process();
            }
        }
    } else {
        // 현재 실행 중인 프로세스가 없으면 스케줄링 시도
        schedule_next_process();
    }
    
    // 간트 차트에 모든 프로세스 상태 기록 (모든 상태 변경 후에 기록)
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
}

void child_signal_handler(int sig) {
    if (sig == SIGTERM) {
        child_should_exit = 1;
    }
    // SIGUSR1은 단순히 "실행 중"임을 나타내는 용도로만 사용
}

int find_next_ready_process() {
    // 우선순위 기반 스케줄링: 가장 높은 우선순위(낮은 숫자)의 READY 프로세스 찾기
    int best_index = -1;
    int best_priority = MAX_PRIORITY + 1;
    
    // 같은 우선순위 내에서 라운드 로빈을 위해 last_scheduled 다음부터 탐색
    int start = (last_scheduled + 1) % num_processes;
    
    for (int i = 0; i < num_processes; i++) {
        int index = (start + i) % num_processes;
        if (pcb_table[index].state == READY) {
            // 더 높은 우선순위(낮은 숫자) 또는 같은 우선순위면 먼저 만난 것 선택
            if (pcb_table[index].priority < best_priority) {
                best_priority = pcb_table[index].priority;
                best_index = index;
            }
        }
    }
    
    return best_index;
}

void schedule_next_process() {
    int next = find_next_ready_process();
    
    if (next != -1) {
        current_process = next;
        last_scheduled = next;
        pcb_table[current_process].state = RUNNING;
        pcb_table[current_process].aging_counter = 0;
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

void apply_aging() {
    // READY 상태 프로세스의 에이징 처리
    for (int i = 0; i < num_processes; i++) {
        if (pcb_table[i].state == READY) {
            pcb_table[i].aging_counter++;
            
            // 에이징 간격마다 우선순위 증가 (숫자 감소 = 더 높은 우선순위)
            if (pcb_table[i].aging_counter >= AGING_INTERVAL) {
                if (pcb_table[i].priority > MIN_PRIORITY) {
                    pcb_table[i].priority -= AGING_AMOUNT;
                    if (pcb_table[i].priority < MIN_PRIORITY) {
                        pcb_table[i].priority = MIN_PRIORITY;
                    }
                    
                    // 에이징 출력: 초기 우선순위가 낮았던(3이상) 프로세스가 처음으로 최고 우선순위(0) 도달할 때만
                    if (pcb_table[i].initial_priority >= 3 && 
                        pcb_table[i].priority == 0 && 
                        pcb_table[i].reached_top == 0) {
                        printf("[에이징] P%d: 초기 %d → 현재 0 ★ 최고 우선순위 도달!\n",
                               i, pcb_table[i].initial_priority);
                        pcb_table[i].reached_top = 1;
                    }
                }
                pcb_table[i].aging_counter = 0;
            }
        }
    }
}

void print_status() {
    printf("\n--- 시스템 상태 (시간: %d) ---\n", current_time);
    printf("프로세스\t상태\t\t우선순위\t퀀텀\tCPU 버스트\tI/O 대기\t대기 시간\n");
    printf("--------------------------------------------------------------------------------\n");
    
    for (int i = 0; i < num_processes; i++) {
        const char *state_str;
        switch (pcb_table[i].state) {
            case READY: state_str = "READY"; break;
            case RUNNING: state_str = "RUNNING"; break;
            case SLEEP: state_str = "SLEEP"; break;
            case DONE: state_str = "DONE"; break;
            default: state_str = "UNKNOWN";
        }
        
        printf("%d\t%s\t\t%d\t\t%d\t%d\t\t%d\t\t%d\n",
               i, state_str, 
               pcb_table[i].priority,
               pcb_table[i].remaining_quantum,
               pcb_table[i].cpu_burst,
               pcb_table[i].io_wait_time,
               pcb_table[i].wait_time);
    }
    printf("완료: %d/%d\n\n", completed_processes, num_processes);
}

void calculate_statistics() {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                        최종 통계                               ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    printf("스케줄링 알고리즘: 우선순위 큐 + 에이징\n");
    printf("사용된 타임 퀀텀: %d\n", time_quantum);
    printf("에이징 간격: %d초\n", AGING_INTERVAL);
    printf("총 시뮬레이션 시간: %d\n", current_time);
    
    int total_wait_time = 0;
    int total_turnaround_time = 0;
    int process_count = 0;
    
    // 종료 순서 기록
    int completion_order[MAX_PROCESSES];
    int completion_times[MAX_PROCESSES];
    for (int i = 0; i < num_processes; i++) {
        completion_order[i] = i;
        completion_times[i] = pcb_table[i].completion_time;
    }
    // 종료 시간순 정렬
    for (int i = 0; i < num_processes - 1; i++) {
        for (int j = i + 1; j < num_processes; j++) {
            if (completion_times[i] > completion_times[j]) {
                int temp = completion_order[i];
                completion_order[i] = completion_order[j];
                completion_order[j] = temp;
                temp = completion_times[i];
                completion_times[i] = completion_times[j];
                completion_times[j] = temp;
            }
        }
    }
    
    printf("\n");
    printf("┌─────────────────────────────────────────────────────────────────┐\n");
    printf("│                    📊 프로세스별 상세 결과                      │\n");
    printf("├─────────┬──────────┬──────────┬──────────┬──────────────────────┤\n");
    printf("│ 프로세스│초기우선순│ 대기시간 │턴어라운드│        비고          │\n");
    printf("├─────────┼──────────┼──────────┼──────────┼──────────────────────┤\n");
    
    for (int i = 0; i < num_processes; i++) {
        if (pcb_table[i].state == DONE && pcb_table[i].completion_time != -1) {
            int turnaround = pcb_table[i].completion_time - pcb_table[i].start_time;
            total_wait_time += pcb_table[i].wait_time;
            total_turnaround_time += turnaround;
            process_count++;
            
            // 비고 생성
            char note[50] = "";
            if (pcb_table[i].initial_priority >= 3) {
                strcpy(note, "⬆️ 낮은순위→실행됨");
            } else if (pcb_table[i].initial_priority <= 1) {
                strcpy(note, "최초 높은 우선순위");
            }
            
            printf("│   P%-4d │    %2d    │   %4d   │   %4d   │ %-20s│\n", 
                   i, pcb_table[i].initial_priority, pcb_table[i].wait_time, turnaround, note);
        }
    }
    printf("└─────────┴──────────┴──────────┴──────────┴──────────────────────┘\n");
    
    // 에이징 효과 분석
    printf("\n");
    printf("┌─────────────────────────────────────────────────────────────────┐\n");
    printf("│                    🔄 에이징 효과 분석                          │\n");
    printf("├─────────────────────────────────────────────────────────────────┤\n");
    
    // 종료 순서 출력
    printf("│ 종료 순서: ");
    for (int i = 0; i < num_processes && i < 10; i++) {
        printf("P%d", completion_order[i]);
        if (i < num_processes - 1 && i < 9) printf(" → ");
    }
    printf("\n");
    
    // 우선순위 역전 분석
    int reversals = 0;
    printf("│                                                                 │\n");
    printf("│ 우선순위 역전 발생:                                             │\n");
    
    for (int i = 0; i < num_processes; i++) {
        int pi = completion_order[i];
        for (int j = i + 1; j < num_processes; j++) {
            int pj = completion_order[j];
            // 초기 우선순위가 낮았던(숫자 큰) 프로세스가 먼저 끝났으면 역전
            if (pcb_table[pi].initial_priority > pcb_table[pj].initial_priority) {
                if (reversals < 5) {  // 최대 5개만 표시
                    printf("│   • P%d(초기:%d)가 P%d(초기:%d)보다 먼저 종료! ✓           │\n",
                           pi, pcb_table[pi].initial_priority,
                           pj, pcb_table[pj].initial_priority);
                }
                reversals++;
            }
        }
    }
    
    if (reversals == 0) {
        printf("│   (역전 없음 - 초기 우선순위 순서대로 종료됨)                  │\n");
    } else if (reversals > 5) {
        printf("│   ... 외 %d건 더                                              │\n", reversals - 5);
    }
    
    printf("│                                                                 │\n");
    printf("│ 📈 에이징 효과: 총 %d건의 우선순위 역전 발생!                   │\n", reversals);
    if (reversals > 0) {
        printf("│    → 낮은 우선순위 프로세스도 기아 없이 실행됨 ✓              │\n");
    }
    printf("└─────────────────────────────────────────────────────────────────┘\n");
    
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
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                         간트 차트                              ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n\n");
    
    int total_time = current_time;
    if (total_time > MAX_TIME) total_time = MAX_TIME;
    if (total_time > 200) total_time = 200;  // 화면에 맞게 최대 200칸
    
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