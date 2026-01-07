

#ifndef __FUNCSCOPE_H__
#define __FUNCSCOPE_H__

#include <stdint.h>

/**
 * @brief 在被检测的进程中调用该函数初始化资源
 * @param num_checkpoints
 *  需要被监测点的数量，最大支持的监测点的数量是 128 个，但是该参数最大可传入的值是 127
 * @return 失败返回 0，成功返回 1
 */
int32_t funcscope_caller_initialize(int8_t num_checkpoints);

/**
 * @brief 在被检测的进程中调用该接口清理资源
 */
int32_t funcscope_caller_cleanup(void);

#endif