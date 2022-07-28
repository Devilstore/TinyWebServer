/*
日志类：

    采用 单例模式(懒汉模式) 进行读写日志
    1. 也可以使用 双检测锁 + 类静态实例 实现懒汉模式
*/

#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

// 日志类
class Log
{
public:
    // C++11 之后，使用局部静态变量 懒汉模式 无需加锁处理
    // 单例模式
    static Log *getInstance()
    {
        static Log instance;
        return &instance;
    }

    // 异步写日志回调函数。取 阻塞队列日志 写入文件
    static void *async_write(void *args)
    {
        // printf("调用 异步写日志函数\n");
        Log::getInstance()->async_wirte_log();
        // printf("调用成功\n");
    }

    // 日志文件， 日志缓冲区大小，日志最大行数，最长日志阻塞队列大小
    bool init(const char *f_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    // 日志 生成函数 (异步 产生日志 加入阻塞队列， 同步情况下直接写入日志)
    void write_log(int level, const char *fromat, ...);

    // 强制刷新 写入文件流 缓冲区
    void flush(void);

    static int m_close_flag; // 关闭日志 标记

private:
    // 私有化 构造函数
    Log();
    virtual ~Log();

    // 异步写日志 函数
    void *async_wirte_log()
    {
        // 日志线程 正在运行
        printf("异步写日志线程 已启动...\n");
        std::string single_log;
        // 从阻塞队列中 去除一个日志 string, 写入文件

        while (m_log_queue->pop(single_log))
        {
            // printf("取出日志，正在写入....");
            // 队列中取出文件 。 加锁进行 写入文件
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp); // 写入文件
            m_mutex.unlock();
            Log::getInstance()->flush(); // 刷新
        }
        // printf("暂无日志");
    }

private:
    char dir_name[128];                    // 日志路径名
    char log_name[128];                    // 日志文件名
    int m_split_lines;                     // 日志最大行数
    int m_log_buf_size;                    // 日志缓冲区大小
    long long m_count;                     // 日志行数记录
    int m_today;                           // 日志按天进行分类 m_today 记录当前时间是哪一天
    FILE *m_fp;                            // 打开的 log 文件指针
    char *m_buff;                          // 缓冲区。
    Block_queue<std::string> *m_log_queue; // 日志阻塞队列
    bool m_is_async;                       // 是否同步 标志位
    locker m_mutex;                        // 互斥锁
};

// __VA_ARGS__是一个可变参数的宏，定义时宏定义中参数列表的最后一个参数为省略号，在实际使用时会发现有时会加##，有时又不加。

#define LOG_DEBUG(format, ...)                                   \
    if (!Log::m_close_flag)                                      \
    {                                                            \
        Log::getInstance()->write_log(0, format, ##__VA_ARGS__); \
    }

#define LOG_INFO(format, ...)                                    \
    if (!Log::m_close_flag)                                      \
    {                                                            \
        Log::getInstance()->write_log(1, format, ##__VA_ARGS__); \
    }

#define LOG_WARN(format, ...)                                    \
    if (!Log::m_close_flag)                                      \
    {                                                            \
        Log::getInstance()->write_log(2, format, ##__VA_ARGS__); \
    }

#define LOG_ERROR(format, ...)                                   \
    if (!Log::m_close_flag)                                      \
    {                                                            \
        Log::getInstance()->write_log(3, format, ##__VA_ARGS__); \
    }

#endif