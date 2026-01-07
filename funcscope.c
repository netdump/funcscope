
#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>

/*****************************************************************/

#define FS_MAX_LINE 512

#ifndef HUGETLBFS_MAGIC
#define HUGETLBFS_MAGIC 0x958458f6
#endif

#define FS_FILENAME ".funcscope_%ld"

/*****************************************************************/

/**
 * @brief 再次确认 fs 的类型
 * @param path HugePage 挂载的绝对路径
 * @return 失败返回 0，成功返回 1
 */
static int fs_is_hugetlbfs(const char *path)
{
    struct statfs st;
    if (statfs(path, &st) < 0)
        return 0;
    return st.f_type == HUGETLBFS_MAGIC;
}

/**
 * @brief 尝试 mmap 一个最小 hugepage，成功 => 当前进程“可以使用” hugepage
 * @param dir HugePage 挂载的绝对路径
 * @return 失败返回 0，成功返回 1
 */
static int fs_try_mmap_hugepage(const char *dir)
{
    char file[256];

    snprintf(file, sizeof(file), "%s/huge_test_%d", dir, getpid());

    int fd = open(file, O_CREAT | O_RDWR, 0600);
    if (fd < 0)
        return 0;

    /* 不需要 ftruncate，hugetlbfs 会自动对齐 hugepage */
    void *addr = mmap(NULL,
                      2 * 1024 * 1024, /* 最小 2MB */
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      fd,
                      0);

    int ok = (addr != MAP_FAILED);

    if (ok)
        munmap(addr, 2 * 1024 * 1024);

    close(fd);
    unlink(file);
    return ok;
}

/**
 * @brief HugePage 可用性检测
 * @param found_dir 返回参数，存放 hugepage 的挂载点
 * @param len 描述 found_dir 指向内存空间的大小
 * @return 失败返回 0，成功返回 1
 */
int fs_detect_hugepage(char *found_dir, size_t len)
{
    FILE *fp = fopen("/proc/self/mounts", "r");

    if (!fp) return 1;

    char line[FS_MAX_LINE];

    while (fgets(line, sizeof(line), fp))
    {
        char dev[128], mnt[128], type[64];

        if (sscanf(line, "%127s %127s %63s", dev, mnt, type) != 3)
            continue;

        if (strcmp(type, "hugetlbfs") != 0)
            continue;

        /* 再次确认 fs 类型 */
        if (!fs_is_hugetlbfs(mnt))
            continue;

        /* 尝试 mmap，作为最终裁决 */
        if (fs_try_mmap_hugepage(mnt))
        {
            strncpy(found_dir, mnt, len);
            fclose(fp);
            return 1; /* 可以使用 hugepage */
        }
    }

    fclose(fp);
    return 0; /* 不可用 */
}

/*****************************************************************/

/* 存放映射后的内存地址 */
static void * fs_mmap_addr = NULL;
/* 存放映射文件的路径，带有文件名 */
static char fs_mmap_path[128] = {0};


/**
 * @brief 在被检测的进程中调用该函数初始化资源
 * @param num_checkpoints
 *  需要被监测点的数量，最大支持的监测点的数量是 128 个，但是该参数最大可传入的值是 127
 * @return 失败返回 0，成功返回 1
 */
int32_t funcscope_caller_initialize(int8_t num_checkpoints)
{

    char dir[128] = {0};

    if (num_checkpoints <= 0) return 0;

    num_checkpoints * sizeof(int32_t);

    if (fs_detect_hugepage(dir, 128)) 
    {

    }
    else 
    {

    }

    return 1;
}

/**
 * @brief 在被检测的进程中调用该接口清理资源
 */
int32_t funcscope_caller_cleanup(void) 
{

    return 1;
}

/*****************************************************************/