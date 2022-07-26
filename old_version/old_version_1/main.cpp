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

#define MAX_FD 10000           // 最大文件描述符个数
#define MAX_EVENT_NUMBER 10000 // epoll最大监听文件描述符数量

// 向 epfd 添加需要监听的 fd
extern void addfd(int epfd, int fd, bool one_shot);
// 从 epfd 中删除 fd
extern void removefd(int epfd, int fd);
// 设置 fd 非阻塞
extern int setnonblocking(int fd);

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

// 从epfd中 修改 fd. (重置socket 的 oneshot
// 属性，确保下一次可读时，EPOLLIN事件会被触发) ev 为想要修改成的事件EPOLLIN等
extern void modifyfd(int epfd, int fd, int ev);

// 传入参数 argv 端口号 IP 等。
int main(int argc, char *argv[])
{
    // 参数错误，输出提示。
    if (argc <= 1)
    {
        printf("请按照如下格式运行：%s port_number\n", basename(argv[0]));
        return 0;
    }

    // 获取端口号
    int port = atoi(argv[1]);

    int ret = 0;

    // 对信号进行处理
    addsig(SIGPIPE, SIG_IGN); // 捕捉到 SIGPIPE 信号，进行忽略处理

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
    ret = listen(listenfd, 5);
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

    // 创建线程池，并进行初始化   类似STL模板类
    threadpool<http_conn> *pool = NULL; // http_connect 为任务类
    try
    {
        pool = new threadpool<http_conn>; // 尝试 初始化线程池
    }
    catch (...)
    {
        return 0; // 初始化线程池失败 直接退出。
    }

    bool stop_server = false;

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
                    printf("分配通信fd失败, errno is : %d\n", errno); // 分配通信fd失败
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
                char cip[16] = {0};
                inet_ntop(AF_INET, &client_address.sin_addr.s_addr, cip, sizeof(cip));
                unsigned short cport = ntohs(client_address.sin_port);
                printf("当前客户端ip:%s,端口：%d\n", cip, cport);

                // 将新的客户连接进行初始化，并放入用户数据信息
                users[connfd].init(connfd, client_address);
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 异常事件  对方异常断开 或者 错误等时间:EPOLLRDHUP|EPOLLHUP|EPOLLERR
                // 回调函数 包括 删除epoll注册事件移除链接节点和关闭相应连接
                users[sockfd].close_conn(); // 销毁http任务
            }
            else if (events[i].events & EPOLLIN)
            { // 除了 监听fd 和 pipefd[0] 其他的读事件
                // 读事件就绪, 调用read()读取数据到读缓冲，读取完毕，将任务加入线程请求队列
                if (users[sockfd].read())
                {
                    // 读事件 处理完毕， 加入线程请求任务队列
                    pool->append(users + sockfd); // users + sockfd 为数组首地址 + 偏移量
                }
                else
                {
                    users[sockfd].close_conn(); // 读事件处理失败，关闭 用户请求任务
                }
            }
            else if (events[i].events & EPOLLOUT)
            {
                // 写数据就绪。not keep-alive，wirte返回false，关闭连接
                if (users[sockfd].write() == false)
                {
                    users[sockfd].close_conn(); // 写事件处理失败，关闭 用户请求任务
                }
            }
        }
    }

    close(epfd);     // 关闭 epoll
    close(listenfd); // 关闭 监听fd
    delete[] users;  // 释放用户请求任务信息
    delete pool;     // 释放线程池
    return 0;
}
