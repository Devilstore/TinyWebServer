#include "locker.h"

// 构造函数 初始化互斥锁变量
locker::locker()
{
    // 返回值 不为0 抛出异常
    if (pthread_mutex_init(&m_mutex, NULL) != 0)
    {
        throw std::exception(); // 抛出异常对象
    }
}

// 析构函数 销毁互斥锁
locker::~locker()
{
    pthread_mutex_destroy(&m_mutex); // 销毁互斥变量
}

// 互斥锁加锁
bool locker::lock()
{
    // 返回0 上锁成功
    return pthread_mutex_lock(&m_mutex) == 0;
}

// 互斥锁解锁
bool locker::unlock()
{
    // 返回0 解锁成功
    return pthread_mutex_unlock(&m_mutex) == 0;
}

// 获取互斥量 指针
pthread_mutex_t *locker::get()
{
    return &m_mutex;
}
//---------------------------

// 构造函数 初始化条件变量
cond::cond()
{
    if (pthread_cond_init(&m_cond, NULL) != 0)
    {
        throw std::exception();
    }
}

// 析构函数 销毁条件变量
cond::~cond()
{
    pthread_cond_destroy(&m_cond);
}

// 等待条件信号
bool cond::wait(pthread_mutex_t *mutex)
{
    int ret = 0;
    ret = pthread_cond_wait(&m_cond, mutex);
    return ret == 0;
}

// 定时等待条件信号
bool cond::timewait(pthread_mutex_t *mutex, struct timespec t)
{
    int ret = 0;
    ret = pthread_cond_timedwait(&m_cond, mutex, &t);
    return ret == 0;
}

// 发送唤醒信号 : 唤醒一个 阻塞进程/线程
bool cond::signal()
{
    return pthread_cond_signal(&m_cond) == 0;
}
bool cond::boradcast()
{
    return pthread_cond_broadcast(&m_cond) == 0;
}

// 构造函数 初始化条件变量
sem::sem()
{
    // _pshared : 0 为线程共享，非0 进程共享
    if (sem_init(&m_sem, 0, 0) != 0)
    {
        throw std::exception();
    }
}

// 构造函数 以val值初始化条件变量
sem::sem(int val)
{
    if (sem_init(&m_sem, 0, val) != 0)
    {
        throw std::exception();
    }
}

// 析构函数 销毁信号量
sem::~sem()
{
    sem_destroy(&m_sem);
}

// 减少信号量（获取资源）P 操作
bool sem::wait()
{
    return sem_wait(&m_sem) == 0;
}

// 增加信号量（释放资源）V 操作
bool sem::post()
{
    return sem_post(&m_sem) == 0;
}
