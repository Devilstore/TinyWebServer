#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include <pthread.h>
#include <string.h>

#include "log.h"

int Log::m_close_flag = 1; // 关闭日志 标记

Log::Log() : m_count(0),
             m_is_async(false) {}

Log::~Log()
{
    if (m_fp)
    {
        fclose(m_fp);
    }
}

// 日志文件，关闭日志标记， 日志缓冲区大小，日志最大行数，最长日志阻塞队列大小
// 同步不需要设置 阻塞队列大小，  异步 需要设置
bool Log::init(const char *f_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    // 如果 堵塞队列大小为0 表示同步更新日志。  不为0，表示异步更新日志。

    if (max_queue_size >= 1)
    {
        m_is_async = true;                                          // 表示 执行 异步 日志
        m_log_queue = new Block_queue<std::string>(max_queue_size); // 申请 阻塞队列
        pthread_t tid;                                              // 日志线程 id

        pthread_create(&tid, NULL, async_write, NULL); // flush_log_thread 做为回调函数，表示创建线程 异步写入日志。

        // pthread_detach(tid); // 设置线程分离
    }

    if (max_queue_size == 0)
    {
        printf("同步 写日志 >>> \n");
    }
    // 其他设置  同步 异步 都需要 初始化的参数
    m_close_flag = close_log;
    m_log_buf_size = log_buf_size;
    m_buff = new char[m_log_buf_size]; // 申请缓冲区空间
    memset(m_buff, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    time_t t = time(NULL);             // 获取当期系统时间
    struct tm *sys_tm = localtime(&t); // time_t 转化为 格式化时间
    struct tm my_tm = *sys_tm;

    const char *p = strrchr(f_name, '/'); // 返回 f_name 中最后一次出现字符 '/' 的位置,如果未找到该值，则函数返回一个空指针。
    char log_full_name[256] = {0};        // 日志文件名 全称

    if (p == NULL) // 找不到 说明日志在当前文件夹下
    {
        // 日志文件名 形式 ：           年_月_日_文件名
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, f_name);
    }
    else
    {
        // 找 到 说明日志在 当前文件夹的 子目录中。找到最后一个
        strcpy(log_name, p + 1);                   // 不包含路径的文件名 copy 到log_name
        strncpy(dir_name, f_name, p + 1 - f_name); // p + 1 为 文件名的第一个字符地址，f_name为整个路径的第一个字符地址。只差为路径名 包含 '/'

        // 子目录下  log 全名 需要加入 路径/年_月_日_文件名
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, f_name);
    }

    m_today = my_tm.tm_mday;          // 当天时间更新
    m_fp = fopen(log_full_name, "a"); // 打开日志文件
    if (m_fp == NULL)
    {
        // printf("未找到日志文件。\n");
        return false;
    }
    // printf("找到日志文件。\n");
    return true;
}

// 日志 生成函数 (异步 产生日志 加入阻塞队列， 同步情况下直接写入日志)
void Log::write_log(int level, const char *fromat, ...)
{
    // printf("执行 write_log.\n");
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL); // 获取当前时间

    time_t t = now.tv_sec; // 当前秒数
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    char s[16] = {0};

    // 加入 level 信息到 s 中
    switch (level)
    {
    case 0:
        stpcpy(s, "[debug]:");
        break;
    case 1:
        stpcpy(s, "[info]:");
        break;
    case 2:
        stpcpy(s, "[warn]:");
        break;
    case 3:
        stpcpy(s, "[erro]:");
        break;
    default:
        stpcpy(s, "[erro]:");
        break;
    }

    // 查看 当前日志 是否为最新文件 并 可以写入
    m_mutex.lock();
    ++m_count; // 日志行数增加

    // 如果当前文件日志 达到最大行数，或者 当前文件日志 时间 不匹配。
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0)
    {
        // printf("执行 write_log 更新文件名.\n");
        char newLogName[256] = {0};
        fflush(m_fp); // 强制刷新 当前日志文件
        fclose(m_fp); // 并关闭 当前日志文件
        char tail[16] = {0};

        // 获取 新的文件名
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        // 日期不匹配时，将新的文件名 写入到 newLog
        if (m_today != my_tm.tm_mday)
        {
            snprintf(newLogName, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday; // 更新当前 日期
            m_count = 0;             // 当前文件行数，一个新文件，重新从0计数
        }
        else
        {
            // 如果是达到最大行数， 则当前日期，创建第二个文件。m_count / m_split_lines;
            snprintf(newLogName, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(newLogName, "a"); // m_fp 更新为 新文件指针
    }

    m_mutex.unlock();

    va_list args;
    va_start(args, fromat);

    // 正式 生成日志
    std::string log_str;
    m_mutex.lock();

    // printf("执行 write_log 生成日志.\n");
    // 写入 具体的 时间 + 日志类型  (年-月-日 时-分-秒-毫秒 [debug] [info] [erro] 等)
    // 标准化 日志行 前缀
    int n = snprintf(m_buff, 48, "%d-%02d-%02d %02d:%02d:%02d.%03ld %s",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec / 1000, s);

    // 写入 当前行 日志内容。  args 即传入的 可变参数。             vsnprintf 与 snprintf的不同之处就在于 一个接收可变参数，一个接收va_list
    int m = vsnprintf(m_buff + n, m_log_buf_size - 1, fromat, args); // m_buff + n,为写入 日志行前缀 后的地址。

    m_buff[m + n] = '\n'; // 手动在 日志行 末尾置 换行符。
    m_buff[m + n + 1] = '\0';
    log_str = m_buff; // 当前 日志行 赋值给 log_str;

    m_mutex.unlock();

    // 如果为异步 并且 阻塞队列不满 则加入阻塞队列
    if (m_is_async && !m_log_queue->isFull())
    {
        // printf("执行 write_log 加入异步日志 队列.\n");
        m_log_queue->push(log_str);
    }
    else
    { // 同步 情况下 直接 fputs 写入 m_fp
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(args);
}

// 强制刷新 写入文件流 缓冲区
void Log::flush(void)
{
    m_mutex.lock();
    fflush(m_fp); // 强制刷新 写入文件流 缓冲区
    m_mutex.unlock();
}