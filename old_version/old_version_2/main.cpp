#include <arpa/inet.h> // socket通信
#include <errno.h>     // 错误代码
#include <fcntl.h>     // 文件操作
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <assert.h>
#include <unistd.h> // unixstd

#include "http_conn.h"
#include "locker.h"
#include "threadpool.h"
#include "log.h"

#define MAX_FD 10000           // 最大文件描述符个数
#define MAX_EVENT_NUMBER 10000 // epoll最大监听文件描述符数量
#define TIMESLOTS 5            // ALARM 信号 产生间隔

// 向 epfd 添加需要监听的 fd
extern void addfd(int epfd, int fd, bool one_shot);
// 从 epfd 中删除 fd
extern void removefd(int epfd, int fd);
// 设置 fd 非阻塞
extern int setnonblocking(int fd);

extern void back_func(http_conn *user_data);

// 添加sig信号捕捉。  param ： sig  函数指针 handler
void addsig(int sig, void(handler)(int))
{
    // printf("addsig\n");
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sa.sa_flags = 0;
    sigfillset(&sa.sa_mask);                 // 添加临时信号阻塞集。信号捕捉完成之后 就不在阻塞
    assert(sigaction(sig, &sa, NULL) != -1); // 注册信号捕捉
}

// ALARM 信号处理函数。 将 alarm 信号 转化为 char* 发送到pipefd[1] （管道写端）
void alarm_handler(int sig)
{
    // printf("alarm_handler\n");
    int save_errno = errno;
    int msg = sig;
    int ret = send(http_conn::pipefd[1], (char *)&msg, 1, 0);
    // printf("发送信号...%d...%d\n", ret, msg);
    errno = save_errno;
}

// 定时标记 处理函数
void timer_hander()
{
    // printf("timer_handler\n");
    if (http_conn::timer_lst)
    {
        // printf("主线程 处理 timer \n");
        // 定时处理任务，实际上就是调用tick()函数
        http_conn::timer_lst->tick();
    }
    // 因为一次 alarm 调用只会引起一次SIGALARM 信号，所以我们要重新定时
    alarm(TIMESLOTS);
}

// 从epfd中 修改 fd. (重置socket 的 oneshot
// 属性，确保下一次可读时，EPOLLIN事件会被触发) ev 为想要修改成的事件EPOLLIN等
extern void modifyfd(int epfd, int fd, int ev);

// 传入参数 argv 端口号 IP 等。
int main(int argc, char *argv[])
{
    // 初始化日志记录
    Log::getInstance()->init(".ServerLog", 0, 8192, 500000, 0); // 同步测试
    // Log::getInstance()->init(".ServerLog", 0, 8192, 500000, 800); // 异步测试

    // 参数错误，输出提示。
    if (argc <= 1)
    {
        // basename(arg) : 将 文件路径形式的参数 arg 分割，获取最后的文件名
        printf("请按照如下格式运行：%s port_number\n", basename(argv[0]));
        // 写入错误日志
        LOG_ERROR("%s", "epoll failure.");
        return 1;
    }

    LOG_INFO("%s", "server is starting.");
    // 获取端口号
    int port = atoi(argv[1]);

    // 对信号进行处理
    addsig(SIGPIPE, SIG_IGN); // 捕捉到 SIGPIPE 信号，进行忽略处理

    int ret = 0;

    // 创建管道
    ret = socketpair(AF_UNIX, SOCK_STREAM, 0, http_conn::pipefd);
    assert(ret != -1);

    addsig(SIGALRM, alarm_handler); // 捕捉SIGALRM信号，进行处理

    // 创建监听socket
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    // 绑定监听socket
    struct sockaddr_in saddr;
    saddr.sin_addr.s_addr = INADDR_ANY; // 接受 IP 类型
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);

    // 设置端口复用(绑定之前进行设置复用)
    int reuse = 1; // 1 表示端口复用
    ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    assert(ret != -1);
    ret = bind(listenfd, (struct sockaddr *)&saddr, sizeof(saddr));
    assert(ret != -1);

    // 监听
    ret = listen(listenfd, 8);
    assert(ret != -1);

    // 创建 epoll 对象，事件数组，添加
    epoll_event events[MAX_EVENT_NUMBER];
    int epfd = epoll_create(100);
    assert(epfd != -1);

    // 创建 客户连接请求数据
    http_conn *users = new http_conn[MAX_FD];
    http_conn::m_epfd = epfd; // 初始化 http 任务类静态成员变量

    // 将 监听fd 添加到 epfd
    addfd(epfd, listenfd, false); // false 表示 不开启oneshot，会持续通知

    // 将 管道fd 添加到 epfd
    addfd(epfd, http_conn::pipefd[0], false);

    // setnonblocking(http_conn::pipefd[0]);
    // setnonblocking(http_conn::pipefd[1]);

    // 创建线程池，并进行初始化   类似STL模板类
    threadpool<http_conn> *pool = NULL; // http_connect 为任务类
    try
    {
        pool = new threadpool<http_conn>; // 尝试 初始化线程池
    }
    catch (...)
    {
        LOG_ERROR("%s", "create threadpoll failure.");
        return 1; // 初始化线程池失败 直接退出。
    }

    bool stop_server = false;
    bool timeout = false; // 标记当前 是否存在定时信号
    alarm(TIMESLOTS);
    LOG_INFO("%s", "alarm signal is activate.");

    LOG_INFO("%s", "server is listening.");
    // 循环检测 epoll 事件
    while (!stop_server)
    {
        // epoll_wait 的第二个参数为传出参数，传出 events 事件
        int num = epoll_wait(epfd, events, MAX_EVENT_NUMBER, -1); // -1永久阻塞
        // epoll_wait调用错误 返回-1
        if (num < 0 && errno != EINTR)
        {
            printf("epoll failure.\n");
            break;
        }
        // 循环遍历 epoll_wait 返回的事件
        for (int i = 0; i < num; ++i)
        {
            int sockfd = events[i].data.fd;
            // 监听fd 有事件（新客户端连接） 主线程处理 监听fd通知
            if (sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlen = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlen);

                if (connfd < 0)
                {
                    // printf("分配通信fd失败, errno is : %d\n", errno); // 分配通信fd失败
                    continue;
                }

                // 目前连接请求数 超过 最大文件描述符个数
                if (http_conn::m_user_size >= MAX_FD)
                {
                    // 可以给客户端回传一个信息：服务器内部正忙
                    printf("服务器正忙, 断开当前链接。\n");
                    close(connfd); // 关闭当前链接  (分配的连接进行关闭)
                    continue;      // 继续监听
                }

                // 当前客户端 ip
                char ip[16] = {0};
                inet_ntop(AF_INET, &client_address.sin_addr.s_addr, ip, sizeof(ip));
                unsigned short ppport = ntohs(client_address.sin_port);
                printf("当前客户端ip:%s,端口：%d, 分配的通信fd:%d\n", ip, ppport, connfd);
                LOG_INFO("client(%s:%d) is connected.", ip, ppport);

                // LOG_INFO("client(%s:%d) is connected. --- (fd:%d).", ip, ppport, connfd);

                // 将新的客户连接进行初始化， 并放入用户数据信息
                users[connfd].init(connfd, client_address);

                // 创建新 http 定时器
                users[connfd].setTimer(users + connfd, back_func, TIMESLOTS);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                if (sockfd == http_conn::pipefd[0] || sockfd == http_conn::pipefd[1])
                {
                    continue;
                }
                printf("sockfd : %d  ------- ", sockfd);
                printf("main epoll 异常事件.... 触发关闭...    ");
                // 异常事件  对方异常断开 或者 错误等时间:EPOLLRDHUP|EPOLLHUP|EPOLLERR
                // 回调函数 包括 删除epoll注册事件移除链接节点和关闭相应连接

                users[sockfd].m_timer->func(users + sockfd);

                // users[sockfd].close_conn(); // 销毁http任务
            }
            else if ((events[i].events & EPOLLIN))
            {
                if (sockfd == http_conn::pipefd[0])
                {
                    // 处理信号
                    int sig;
                    char signals[1024];
                    ret = recv(sockfd, signals, sizeof(signals), 0);
                    if (ret == -1)
                    {
                        continue;
                    }
                    else if (ret == 0)
                    {
                        continue;
                    }
                    else if (ret > 0)
                    {
                        // 接收到信号 进行逻辑处理
                        for (int i = 0; i < ret; ++i)
                        {
                            // printf("信号 ： signals[i] : %d\n", signals[i]);
                            switch (signals[i])
                            {
                            case SIGALRM:
                            {
                                timeout = true;
                                break;
                            }
                            case SIGTERM:
                            {
                                stop_server = true;
                                break;
                            }
                            }
                        }
                    }
                }
                else
                {
                    // 非 信号的读事件 ： 即客户端 读事件

                    // 除了 监听fd 和 pipefd[0] 其他的读事件
                    // 读事件就绪, 调用read()读取数据到读缓冲，读取完毕，将任务加入线程请求队列
                    if (users[sockfd].read())
                    {
                        // printf("加入线程池...\n");
                        // 读事件 处理完毕， 加入线程请求任务队列
                        pool->append(users + sockfd); // users + sockfd 为数组首地址 + 偏移量
                        // 更新当前 http 任务的定时器
                        if (users[sockfd].m_timer != nullptr)
                        {
                            char ip[16];
                            users[sockfd].getClientIp(ip);
                            // printf("客户端：%s   更新到期时间。\n", ip);
                            // http_conn::timer_lst->update_timer(users[sockfd].m_timer);
                            users[sockfd].setTimer(users + sockfd, back_func, TIMESLOTS); // 调整 http 任务的 时间节点信息
                        }
                    }
                    else
                    {
                        users[sockfd].m_timer->func(users + sockfd);
                        // users[sockfd].close_conn(); // 读事件处理失败，关闭 用户请求任务
                    }
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                // 写数据就绪。not keep-alive，wirte返回false，关闭连接
                if (users[sockfd].write() == false)
                {
                    users[sockfd].m_timer->func(users + sockfd);
                    // users[sockfd].close_conn(); // 写事件处理失败，关闭 用户请求任务
                }
            }
        }
        // 如果存在 信号标记则处理定时时间。先执行I/O事件
        if (timeout)
        {
            // printf("处理...");
            timer_hander();
            timeout = false;
        }
    }

    close(epfd);     // 关闭 epoll
    close(listenfd); // 关闭 监听fd
    delete[] users;  // 释放用户请求任务信息
    delete pool;     // 释放线程池
    return 0;
}
