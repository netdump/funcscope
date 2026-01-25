
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sched.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sched.h>
#include <sys/resource.h>
#include <string.h>
#include <errno.h>

#include "funcscope.h"

/* 设置 CPU 绑定 */
static void bind_to_cpu(int cpu)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
    {
        perror("sched_setaffinity");
    }
    else
    {
        printf("Bound to CPU %d\n", cpu);
    }
}

/* 设置实时调度策略 SCHED_FIFO */
static void set_realtime_priority(int priority)
{
    struct sched_param param;
    param.sched_priority = priority;

    if (sched_setscheduler(0, SCHED_FIFO, &param) != 0)
    {
        perror("sched_setscheduler");
    }
    else
    {
        printf("SCHED_FIFO priority set to %d\n", priority);
    }
}

/* 设置 nice 值，-20 ~ 19 */
static void set_high_nice(int nice_val)
{
    if (setpriority(PRIO_PROCESS, 0, nice_val) != 0)
    {
        perror("setpriority");
    }
    else
    {
        int cur = getpriority(PRIO_PROCESS, 0);
        printf("Nice value set to %d\n", cur);
    }
}

#define WORK_SIZE (1 * 1024) // 64KB，明显跨 L1
#define LOOP_CNT 4           // 总工作量可调
static uint8_t work_buf[WORK_SIZE];

__attribute__((noinline)) void task_func(void)
{

    FUNCSCOPE_ENTER(0);

    #if 1
    uint64_t sum = 0;

    for (int r = 0; r < LOOP_CNT; r++)
    {
        for (size_t i = 0; i < WORK_SIZE; i += 64)
        {
            /* 强制触碰每个 cache line */
            uint64_t *p = (uint64_t *)&work_buf[i];

            sum += p[0];
            sum += p[1];
            sum += p[2];
            sum += p[3];
            sum += p[4];
            sum += p[5];
            sum += p[6];
            sum += p[7];
        }
    }

    /* 防止编译器优化掉 */
    asm volatile("" ::"r"(sum) : "memory");
    #endif
    FUNCSCOPE_EXIT(0);

    return ;
}

int main(void) {

    // 1. 绑定到 CPU0
    bind_to_cpu(7);

    // 2. 设置实时优先级
    set_realtime_priority(80); // 高优先级，SCHED_FIFO

    // 3. 设置 nice 值为 -20
    set_high_nice(-20);

    printf("pid: %d\n", getpid());

    funcscope_caller_initialize(1, FS_LITE);

    sleep(20);

    printf("-------\n");

    while (1) {
        FUNCSCOPE_SERVER_POLL_EVERY_N(65536);
        task_func();
    }

    funcscope_caller_cleanup();

    return 0;
}
