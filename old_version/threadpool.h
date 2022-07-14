#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <list>
#include <exception>
#include <cstdio>
#include "locker.h" // 自己的类 导入

// template <typename T>
// class threadpool;

// 线程池类(模板类),方便 代码复用，模板参数T是任务类
template <typename T>
class threadpool
{
private:
    int m_thread_size;          // 线程的数量
    pthread_t *m_threads;       // 线程池 数组，大小为线程数量
    int m_max_requests;         // 请求队列中最多允许的，等待处理的请求数量
    std::list<T *> m_workqueue; // 请求队列 (所有线程共享)  所有线程都属于 线程池 类对象
    locker m_queuelocker;       // 互斥锁
    sem m_queuestat;            // 信号量： 用于判断是否有任务需要处理
    bool m_stop;                // 是否结束线程

private:
    static void *worker(void *arg); // 工作函数 (调用run()),它不断从工作队列中取出任务并执行之
    void run();                     // 线程池 实际工作函数，调用http::conn对象函数处理

public:
    threadpool(int thread_size = 8, int max_requsts = 10000); // 构造函数， 默认构造
    ~threadpool();                                            // 析构函数
    bool append(T *request);                                  // 添加任务
};

// 构造函数， 默认构造
template <typename T>
threadpool<T>::threadpool(int thread_size, int max_requsts)
{
    m_thread_size = thread_size;
    m_max_requests = max_requsts;
    m_stop = false;
    m_threads = NULL;

    // 传入线程数量或最大请求数量 非法
    if (thread_size <= 0 || max_requsts <= 0)
    {
        throw std::exception(); // 抛出异常
    }

    m_threads = new pthread_t[m_thread_size]; // 申请线程池空间

    if (m_threads == NULL)
    {
        throw std::exception(); // 申请空间失败抛出异常
    }

    // 创建 m_thread_size 个线程，并设置线程分离
    for (int i = 0; i < m_thread_size; ++i)
    {
        printf("create the %dth thread.\n", i);
        // 将线程池 类对象 本身 当做参数传入 线程工作函数中（以便于线程调用类对象成员属性）
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads; // 线程创建失败， 抛出异常
            throw std::exception();
        }
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads; // 线程分离失败， 抛出异常
            throw std::exception();
        }
    }
}

// 析构函数
template <typename T>
threadpool<T>::~threadpool()
{
    if (m_threads != NULL)
    {
        delete[] m_threads;
    }
    m_threads = NULL;
    m_stop = true;
}

// 添加任务到请求队列。 请求队列 做为 互斥资源 访问
template <typename T>
bool threadpool<T>::append(T *request)
{
    // 先加锁，然后判断是否 还有空余位置 添加队列
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        // 超出最大容量，返回失败
        m_queuelocker.unlock();
        return false;
    }
    // 加入 任务队列
    m_workqueue.push_back(request);
    m_queuelocker.unlock(); // 解锁
    m_queuestat.post();     // 信号量更新，有新任务需要处理
    return true;
}

// 每个线程都会执行 worker, 然后调用 run 一直运行。没有任务的时候处于阻塞状态
// 传入线程池本身 令静态成员函数，能够访问到 模板类的成员变量
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    // 转换参数为 线程池对象
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
    // 循环取任务执行, 直到stop
    while (!m_stop)
    {
        m_queuestat.wait();      // 等待队列 有任务到来 (阻塞)
        m_queuelocker.lock();    // 上锁，操作任务队列
        if (m_workqueue.empty()) // 如果当前任务队列不存在任务，则继续循环
        {
            m_queuelocker.unlock(); // 解锁处理
            continue;
        }
        T *request = m_workqueue.front(); // 读取任务
        m_workqueue.pop_front();          // 从任务队列删除任务
        m_queuelocker.unlock();           // 解锁处理
        if (request == NULL)              // 获取任务失败，继续循环
        {
            continue;
        }
        request->process(); // 任务类处理函数
    }
}

#endif