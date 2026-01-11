# funcscope

## 项目简介

funcscope 是一个 **函数级 / 代码片段级执行耗时采集工具**，面向 **Linux x86_64 与 ARMv8 平台**，通过 **源码插桩** 精确测量执行时间。  

它并非通用 Profiler，而是用于 **量化微优化效果与验证性能回归** 的低侵入度量工具。  

funcscope 在函数或代码片段的入口 / 退出处记录时间差，并写入每个监测点独立的固定大小缓冲区，实现：

- 零锁  
- 零原子操作  
- 零系统调用（热路径）  

统计与分析由独立 tool 进程完成，被测进程仅负责写入原始耗时数据。  

与 perf 的采样模型不同，funcscope 提供的是 **确定性的单次调用耗时数据**。  

适用于网络代理、协议栈、事件驱动程序等高频、性能敏感场景。  

---

## 核心 API 宏规范

### 插桩宏

- **FUNCSCOPE_ENTER(idx)**  
  - 标记函数或代码片段开始  
  - `idx` 为监测点索引，范围 `0 ~ num_checkpoints-1`  
  - 内部仅读取 TSC / CNTVCT，耗时极低  

- **FUNCSCOPE_EXIT(idx)**  
  - 标记函数或代码片段结束  
  - 将耗时写入对应缓冲区（环形数组）  
  - 内部仅修改 `write_pos` 与数组内容，**两条 cache line 写入**  

**注意：**

- 必须成对使用  
- 不支持嵌套或递归  
- 单次耗时必须 < 1 秒  

---

### 初始化与清理

- **int32_t funcscope_caller_initialize(uint8_t num_checkpoints, int32_t level)**  
  - 初始化 funcscope 资源  
  - 每个进程单独初始化  
  - num_checkpoints: 最大 128  
  - level: FS_LITE / FS_NORMAL / FS_DEEP / FS_FULL  
  - 返回 1 表示成功，0 表示失败  

- **int32_t funcscope_caller_cleanup(void)**  
  - 释放当前进程资源  
  - 设置退出标志供 tool 感知  
  - 返回 1 表示成功  

---

### Server Attach 接口

- **int funcscope_server_poll_and_send_fd(void)**  
  - 非阻塞轮询 Unix Domain Socket  
  - 向 tool 发送 mmap backing fd  
  - 返回 1: 成功发送 fd  
  - 返回 0: 无 tool attach  
  - 返回 -1: 发生错误（可忽略或记录）  

推荐调用：

```
for (;;) {
event_loop_process();
funcscope_server_poll_and_send_fd();
}
```

**注意：**

- 仅在主循环或非热路径调用  
- 不要在 FUNCSCOPE_ENTER/EXIT 内调用  

---

### 平台时间读取

- x86_64: RDTSC  
- ARMv8: CNTVCT_EL0  
- 内部通过 inline 函数直接读取 CPU tick，开销极低  

---

## 使用示例

**初始化：**

```
uint8_t checkpoints = 64;
int32_t level = FS_DEEP;
if (!funcscope_caller_initialize(checkpoints, level)) {
// 初始化失败处理
}
```

**函数 / 代码片段插桩：**

```
FUNCSCOPE_ENTER(0);
/* hot path or critical section */
FUNCSCOPE_EXIT(0);
```

**主循环中 tool attach：**
```
for (;;) {
event_loop_process();
funcscope_server_poll_and_send_fd();
}
```

**清理资源：**

```
funcscope_caller_cleanup();
```


---

## 能力边界与限制

### 支持的场景

- 函数级 / 代码片段级执行耗时测量  
- 高频调用路径微优化验证  
- 偶发慢路径（尾部延迟）统计  
- 单线程、单进程写入  
- Linux x86_64 / ARMv8  

### 不支持的场景

- 多线程写入  
- 递归函数  
- 线程调度 / 上下文切换分析  
- 自动热点定位  
- 跨函数调用栈分析  

---

## 进程与线程模型

- **写入**: 单线程，单进程  
- **多进程**: 每个子进程需单独初始化  
- **tool**: 只读，通过 mmap 获取数据  
- **热路径**: FUNCSCOPE_ENTER / EXIT 完全 O(1)  

---

## 性能与内存

- 每个监测点独立缓存行对齐（64B）  
- 热路径只访问 2 个 cache line  
- 无锁、无原子操作、无系统调用  
- mmap 空间固定、可预测  
- 可选 hugepage 或 4K page 映射  

---

## 提供形式

- 以 **源码形式提供**  
- 文件包括：funcscope.h / funcscope.c / tool 示例  
- 不提供 `.so` 或 `.a`  

设计原因：

- FUNCSCOPE 宏必须在编译期展开  
- 保证 inline 汇编 / cacheline 行为  
- 便于裁剪与优化  

推荐直接纳入项目源码树使用。  

---

## 与 perf / bpftrace 的关系

| 工具      | 擅长                             | 不擅长       |
| --------- | -------------------------------- | ------------ |
| perf      | 全局热点、cache miss、指令级分析 | 精确源码边界 |
| bpftrace  | 动态追踪、低侵入                 | 高频路径     |
| funcscope | 已知代码位置的精确耗时           | 全局分析     |

推荐流程：

1. 使用 perf 定位热点函数  
2. 缩小源码范围  
3. 使用 funcscope 精确度量关键路径  

---

# FUNCSCOPE 宏 API 规范表

| 宏 / 函数                                           | 参数                                                                                 | 功能说明                                                 | 热路径开销                                    | 注意事项                                                     |
| --------------------------------------------------- | ------------------------------------------------------------------------------------ | -------------------------------------------------------- | --------------------------------------------- | ------------------------------------------------------------ |
| FUNCSCOPE_ENTER(idx)                                | idx: 监测点索引 (0 ~ num_checkpoints-1)                                              | 记录函数或代码片段开始时间                               | 1 条 RDTSC / CNTVCT 指令 + 条件判断           | 必须与 FUNCSCOPE_EXIT 成对使用；不支持递归或嵌套调用         |
| FUNCSCOPE_EXIT(idx)                                 | idx: 监测点索引 (0 ~ num_checkpoints-1)                                              | 记录函数或代码片段结束时间，并写入环形缓冲区             | 1 条 RDTSC / CNTVCT + 2 次 cacheline 写入     | 热路径访问 2 个 cache line；保证 idx 合法；不支持递归或嵌套  |
| funcscope_caller_initialize(num_checkpoints, level) | num_checkpoints: uint8_t, 最大 128<br>level: FS_LITE / FS_NORMAL / FS_DEEP / FS_FULL | 初始化 funcscope 资源，mmap 分配缓冲区，初始化 server fd | 非热路径，仅初始化时调用                      | 每个进程单独调用；返回 1 成功，0 失败                        |
| funcscope_caller_cleanup()                          | 无                                                                                   | 清理 funcscope 资源，设置退出标志，munmap 内存           | 非热路径，仅清理时调用                        | 每个进程调用一次；返回 1 成功                                |
| funcscope_server_poll_and_send_fd()                 | 无                                                                                   | 非阻塞轮询 Unix Domain Socket，将 mmap fd 发送给 tool    | 非热路径，可放入主循环                        | 仅在非热路径或循环中调用；返回 1 发送成功，0 无连接，-1 出错 |
| funcscope_rdtsc()                                   | 无                                                                                   | 内部使用，获取 CPU 时钟计数                              | 1 条汇编指令（x86: RDTSC，ARMv8: CNTVCT_EL0） | 不直接使用，FUNCSCOPE_ENTER/EXIT 内部调用                    |
| funcscope_hugepage_mmap(dir, size)                  | dir: hugepage 挂载路径<br>size: 映射大小                                             | mmap hugepage 文件，返回可用地址                         | 初始化时调用                                  | 非热路径；size 会向上对齐 2MB                                |
| funcscope_file_mmap_4K(dir, size)                   | dir: tmpfs 或文件目录<br>size: 映射大小                                              | mmap 常规文件，返回可用地址                              | 初始化时调用                                  | 非热路径；size 会向上对齐 4KB                                |



## 一句话总结

funcscope 是一个 **源码级、单线程写、单进程可控、tool 只读的函数耗时度量工具**。  

它不回答“哪里慢”，只回答：

> 这段代码，真实跑了多久。  

