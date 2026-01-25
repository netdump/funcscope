
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "funcscope.h"

#define MAX_PIDS            32
#define MAX_EVENTS          32

#define FUNCSCOPE_SOCK_DIR "/tmp"
#define FUNCSCOPE_SOCK_FMT "/tmp/funcscope.%d.sock"

typedef struct {
    pid_t pid;
    int sockfd;
    int mmap_fd;
    funcscope_t *fs;
    int attached;
} target_proc_t;

typedef struct
{
    pid_t pids[MAX_PIDS];
    int pid_count;
    int interval_ms;
    int repeat; /* <0 表示 infinite */
} tool_args_t;

static void usage(const char *prog)
{
    printf(
        "Usage: %s -p <pid> [-p <pid> ...] [-i interval_ms] [-r repeat]\n"
        "\n"
        "Options:\n"
        "  -p pid         target process pid (can be repeated)\n"
        "  -i interval    output interval in ms (default 1000)\n"
        "  -r repeat      output times, <=0 means infinite\n"
        "  -h             show this help\n",
        prog);
}

static int pid_valid(pid_t pid)
{
    if (pid <= 0)
        return 0;
    return (kill(pid, 0) == 0 || errno == EPERM);
}

static int parse_args(int argc, char **argv, tool_args_t *args)
{
    memset(args, 0, sizeof(*args));
    args->interval_ms = 1000;
    args->repeat = -1;

    int opt;
    while ((opt = getopt(argc, argv, "p:i:r:h")) != -1)
    {
        switch (opt)
        {
            case 'p':
                if (args->pid_count >= MAX_PIDS)
                {
                    fprintf(stderr, "too many pids\n");
                    return -1;
                }
                pid_t pid = atoi(optarg);
                if (!pid_valid(pid))
                {
                    fprintf(stderr, "invalid pid: %d\n", pid);
                    return -1;
                }
                args->pids[args->pid_count++] = pid;
                break;
            case 'i':
                args->interval_ms = atoi(optarg);
                break;
            case 'r':
                args->repeat = atoi(optarg);
                break;
            case 'h':
            default:
                usage(argv[0]);
                exit(0);
        }
    }

    if (args->pid_count == 0)
    {
        usage(argv[0]);
        return -1;
    }
    return 0;
}

static void build_sock_path(pid_t pid, char *buf, size_t len)
{
    snprintf(buf, len, FUNCSCOPE_SOCK_FMT, pid);
}

static int connect_target(pid_t pid)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    build_sock_path(pid, addr.sun_path, sizeof(addr.sun_path));

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        if (errno != EINPROGRESS)
        {
            close(fd);
            return -1;
        }
    }
    return fd;
}

static int recv_mmap_fd(int sockfd)
{
    struct msghdr msg = {0};
    struct iovec iov;
    char dummy;

    char cmsg_buf[CMSG_SPACE(sizeof(int))];

    iov.iov_base = &dummy;
    iov.iov_len = sizeof(dummy);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buf;
    msg.msg_controllen = sizeof(cmsg_buf);

    if (recvmsg(sockfd, &msg, 0) <= 0)
        return -1;

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if (!cmsg)
        return -1;

    return *((int *)CMSG_DATA(cmsg));
}

static int attach_all(target_proc_t *procs, int n)
{
    int epfd = epoll_create1(0);
    struct epoll_event ev, events[MAX_EVENTS];

    for (int i = 0; i < n; i++)
    {
        procs[i].sockfd = connect_target(procs[i].pid);
        assert(procs[i].sockfd >= 0);

        ev.events = EPOLLIN;
        ev.data.u32 = i;
        epoll_ctl(epfd, EPOLL_CTL_ADD, procs[i].sockfd, &ev);
    }

    int attached = 0;
    while (attached < n)
    {
        int nr = epoll_wait(epfd, events, MAX_EVENTS, -1);
        for (int i = 0; i < nr; i++)
        {
            int idx = events[i].data.u32;
            int fd = recv_mmap_fd(procs[idx].sockfd);
            if (fd >= 0)
            {
                procs[idx].mmap_fd = fd;
                procs[idx].attached = 1;
                close(procs[idx].sockfd);
                attached++;
                printf("[+] attached pid %d (%d/%d)\n",
                       procs[idx].pid, attached, n);
            }
        }
    }
    close(epfd);
    return 0;
}

static funcscope_t *mmap_full(int fd) {
    struct stat st;

    if (fstat(fd, &st) < 0)
    {
        perror("fstat mmap fd");
        return NULL;
    }

    printf("st.st_size: %ld; fd: %d\n", st.st_size, fd);

    if (st.st_size < sizeof(funcscope_t))
    {
        fprintf(stderr,
                "invalid mmap size: %zu\n",
                (size_t)st.st_size);
        return NULL;
    }

    void *p = mmap(FUNCSCOPE_MMAP_BASE,
                   st.st_size,
                   PROT_READ,
                   MAP_SHARED | MAP_FIXED,
                   fd,
                   0);
    if (p == MAP_FAILED)
    {
        perror("mmap full");
        return NULL;
    }

    return (funcscope_t *)p;
}

typedef struct
{
    double min, max, avg;
    double p50, p99;
    double stddev;
    double iqr;
} stats_t;

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(uint64_t *)a;
    uint64_t y = *(uint64_t *)b;
    return (x > y) - (x < y);
}

static void compute_stats(funcscope_slot_t *slot, stats_t *out)
{
    uint32_t level = slot->level;
    uint64_t total = slot->write_pos;
    uint32_t n = total < level ? total : level;
    if (n == 0)
        return;

    uint64_t *tmp = alloca(n * sizeof(uint64_t));
    memcpy(tmp, slot->address, n * sizeof(uint64_t));

    qsort(tmp, n, sizeof(uint64_t), cmp_u64);

    double sum = 0;
    for (uint32_t i = 0; i < n; i++)
        sum += tmp[i];

    out->min = tmp[0];
    out->max = tmp[n - 1];
    out->avg = sum / n;
    out->p50 = tmp[n / 2];
    out->p99 = tmp[(uint32_t)(n * 0.99)];

    double var = 0;
    for (uint32_t i = 0; i < n; i++)
        var += (tmp[i] - out->avg) * (tmp[i] - out->avg);
    out->stddev = sqrt(var / n);

    double q1 = tmp[n / 4];
    double q3 = tmp[(n * 3) / 4];
    out->iqr = q3 - q1;
}

static volatile sig_atomic_t stop;

static void on_signal(int sig)
{
    stop = 1;
}

/* ===================== main ===================== */

int main(int argc, char **argv)
{
    tool_args_t args;
    if (parse_args(argc, argv, &args) < 0)
        return 1;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    target_proc_t procs[MAX_PIDS] = {0};
    for (int i = 0; i < args.pid_count; i++)
        procs[i].pid = args.pids[i];

    /* 阶段 2 */
    attach_all(procs, args.pid_count);

    /* 阶段 3 */
    for (int i = 0; i < args.pid_count; i++)
        procs[i].fs = mmap_full(procs[i].mmap_fd);

    /* 阶段 4 */
    int loop = 0;
    while (!stop)
    {
        for (int i = 0; i < args.pid_count; i++)
        {
            funcscope_t *fs = procs[i].fs;
            for (int s = 0; s < fs->private.num_checkpoints; s++)
            {
                stats_t st = {0};
                compute_stats(&fs->slots[s], &st);
                printf("pid %d [%s] min %.0f max %.0f avg %.1f "
                       "p50 %.0f p99 %.0f std %.1f iqr %.1f\n",
                       procs[i].pid,
                       fs->func_name[s],
                       st.min, st.max, st.avg,
                       st.p50, st.p99, st.stddev, st.iqr);
            }
        }

        if (args.repeat > 0 && ++loop >= args.repeat)
            break;
        usleep(args.interval_ms * 1000);
    }

    /* cleanup */
    for (int i = 0; i < args.pid_count; i++)
    {
        munmap(procs[i].fs, procs[i].fs->private.space_size);
        close(procs[i].mmap_fd);
    }
    return 0;
}
