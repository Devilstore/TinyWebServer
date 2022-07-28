#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>   // 多线程  mutex_t cond_t
#include <semaphore.h> // sem信号量
#include <exception>   // 异常类

// 线程同步机制封装类
class locker;
class cond;
class sem;

// 互斥锁 类
class locker
{
public:
    // 构造函数 初始化互斥锁变量
    locker();

    // 析构函数 销毁互斥锁
    ~locker();

    // 互斥锁加锁
    bool lock();

    // 互斥锁解锁
    bool unlock();

    // 获取互斥量 指针
    pthread_mutex_t *get();

private:
    pthread_mutex_t m_mutex; // 互斥锁变量
};

// 条件变量 类
class cond
{
private:
    pthread_cond_t m_cond;

public:
    // 构造函数 初始化条件变量
    cond();

    // 析构函数 销毁条件变量
    ~cond();

    // 等待条件信号
    bool wait(pthread_mutex_t *mutex);

    // 定时等待条件信号
    bool timewait(pthread_mutex_t *mutex, struct timespec t);

    // 发送唤醒信号 : 唤醒一个 阻塞进程/线程
    bool signal();

    // 发送唤醒信号 : 唤醒所有的 阻塞进程/线程
    bool boradcast();
};

// 信号量 类
class sem
{
private:
    sem_t m_sem;

public:
    // 构造函数 初始化条件变量
    sem();

    // 构造函数 以val值初始化条件变量
    sem(int val);

    // 析构函数 销毁信号量
    ~sem();

    // 减少信号量（获取资源）P 操作
    bool wait();

    // 增加信号量（释放资源）V 操作
    bool post();
};
#endif