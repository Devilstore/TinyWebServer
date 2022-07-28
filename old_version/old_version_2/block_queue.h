
/*
阻塞队列，采用循环数组实现。 m_back = (m_back + 1) % m_max_size;
线程安全：对阻塞队列的操作都需要进行 加锁 之后在进行操作，操作完进行解锁。
*/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <deque>

#include "locker.h"

template <typename T>
class Block_queue
{
public:
    // 构造函数
    Block_queue(int max_size = 1000)
        : m_max_size(max_size),
          m_queue()
    {
        // 最大容量 默认1000
        if (max_size <= 0)
        {
            // 初始化失败
            exit(-1);
        }

        if (!m_queue.empty())
        {
            m_queue.clear();
        }
    }

    // 析构函数
    ~Block_queue()
    {
        m_mutex.lock();
        if (m_queue)
        {
            delete m_queue; // 释放内存
        }
        m_mutex.unlock();
    }

    // 清空队列
    void clear()
    {
        m_mutex.lock();
        m_queue.clear();
        m_mutex.unlock();
    }

    // 判断队列是否满
    bool isFull()
    {
        m_mutex.lock();
        bool flag = (m_queue.size() >= m_max_size);
        m_mutex.unlock();
        return flag;
    }

    // 判断队列是否空
    bool isEmpty()
    {
        m_mutex.lock();
        bool flag = m_queue.empty();
        m_mutex.unlock();
        return flag;
    }

    // 返回队首元素
    bool front(T &value)
    {
        m_mutex.lock();
        if (m_queue.empty())
        {
            m_mutex.unlock();
            return false;
        }
        value = m_queue.front();
        m_mutex.unlock();
        return true;
    }

    // 返回队尾元素
    bool back(T &value)
    {
        m_mutex.lock();

        if (m_queue.empty())
        {
            m_mutex.unlock();
            return false;
        }
        value = m_queue.back();
        m_mutex.unlock();
        return true;
    }

    // 返回队列大小
    int size()
    {
        int t = 0;
        m_mutex.lock();
        t = m_queue.size();
        m_mutex.unlock();
        return t;
    }

    // 返回队列容量大小
    int max_size()
    {
        int t = 0;
        m_mutex.lock();
        t = m_max_size;
        m_mutex.unlock();
        return t;
    }

    // 入队
    bool push(const T &value)
    {
        // printf("阻塞队列 尝试放入日志 \n");
        m_mutex.lock();
        if (m_queue.size() >= m_max_size)
        {
            m_cond.boradcast();
            m_mutex.unlock();
            return false;
        }

        // printf("阻塞队列 放文件\n");
        m_queue.emplace_back(value);
        // printf("阻塞队列 放文件成功\n");

        m_cond.boradcast();
        m_mutex.unlock();
        return true;
    }

    // 出队
    bool pop(T &value)
    {
        // printf("阻塞队列 取文件\n");
        m_mutex.lock();
        if (m_queue.empty())
        {
            // cond 一定要配合 mutex 一起用。
            // cond wait的时候 会自动打开 mutex.unlock,
            // 并且当被 boradcast 或者 signal 唤醒的时候再自动加锁
            if (!m_cond.wait(m_mutex.get()))
            {
                m_mutex.unlock();
                return false; // 失败
            }
        }

        // printf("阻塞队列 取文件 成功\n");
        // 如果 if 条件没有执行 也就是 wait 成功， 则当前 阻塞队列存在任务，直接取出即可。
        value = m_queue.front();
        m_queue.pop_front();
        m_mutex.unlock();
        return true;
    }

    // ms_timeout 参数为 毫秒................... 秒 毫秒 微秒 纳秒
    // 出队(定时出队，超时停止)
    bool pop(T &value, int ms_timeout)
    {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL); // 获取当前时间

        m_mutex.lock();
        if (m_queue.empty())
        {
            // timeval 转 timespec
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = now.tv_usec * 1000 + (ms_timeout % 1000) * 1000000; // now 的微秒 变纳秒 + ms_timeout 毫秒 转化为 秒的剩余

            if (!m_cond.timewait(m_mutex.get(), t))
            {
                m_mutex.unlock();
                return false; // 失败
            }
        }

        // 超时结束 需要判断当前是否存在 m_size；
        if (m_queue.empty())
        {
            m_mutex.unlock();
            return false; // 失败
        }

        value = m_queue.front();
        m_queue.pop_front();
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex; // 互斥锁
    cond m_cond;    // 条件变量 生产者-消费者模型

    std::deque<T> m_queue; // 任务队列
    int m_max_size;        // 最大容量
};

#endif