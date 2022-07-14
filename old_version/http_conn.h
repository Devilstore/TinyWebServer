#ifndef HTTPCONNECT_H
#define HTTPCONNECT_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <string.h>
#include "locker.h"

#include <time.h>

class ulist_timer;
class timer_list;
class http_conn;

// http 连接请求 任务类
class http_conn
{
    // 设置友元函数 用以访问 http_conn 对象中的私有变量
    friend void back_func(http_conn *);

public:
    static int *pipefd;           // 传递信号管道。pipe[1] 用于写,pipe[0] 用于读
    static timer_list *timer_lst; // http对象超时任务链表
    ulist_timer *m_timer;         // 每个 users 独有

public:
    // http 任务类共享 epfd属性
    static int m_epfd;      // 所有socket事件都被注册到同一个epoll对象中
    static int m_user_size; // 统计当前用户数量

    // 静态常量类成员变量 可以在类内初始化
    static const int READ_BUF_SIZE = 2048;  // 读缓冲最大容量
    static const int WRITE_BUF_SIZE = 2048; // 写缓冲最大容量
    static const int FILENAME_LEN = 200;    // 文件名的最大长度

    // HTTP请求方法，这里只支持GET
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT
    };

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };

    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}  // 构造函数
    ~http_conn() {} // 析构函数

public:
    void init(int sockfd, const sockaddr_in &addr); // 初始化新接收的 用户连接任务请求
    void close_conn();                              // 销毁 通信连接任务。
    void process();                                 // 工作函数 : 处理客户端请求
    bool read();                                    // 读完 返回真 （非阻塞读；
    bool write();                                   // 写完 返回真 （非阻塞写

private:
    void init();                            // 初始化连接  分析请求相关信息
    HTTP_CODE process_read();               // 解析HTTP请求报文
    bool process_write(HTTP_CODE read_ret); // 生成HTTP响应报文

    // 这一组函数被process_read调用以分析HTTP请求
    HTTP_CODE parse_request_line(char *text);              // 解析请求首行
    HTTP_CODE parse_headers(char *text);                   // 解析请求头部
    HTTP_CODE parse_content(char *text);                   // 解析请求体
    HTTP_CODE do_request();                                // 解析请求
    char *get_line() { return m_read_buf + m_start_line; } // 获取一行数据 返回该行指针即可。
    LINE_STATUS parse_line();                              // 具体解析某一行

    // 这一组函数被process_write调用以生成HTTP响应
    bool add_status_line(int status, const char *title); // 添加 响应 请求首行
    bool add_response(const char *format, ...);          // 向写缓冲区写入待发送的数据
    bool add_headers(int content_len);                   // 添加响应 请求头部
    bool add_content_length(int content_len);            // 添加响应头部信息 : content-length
    bool add_content_type();                             // 添加响应头部信息 : Content-Type
    bool add_linger();                                   // 添加响应头部信息 : Connection:keep-alive
    bool add_blank_line();                               // 添加响应头部信息 : 空行
    bool add_content(const char *content);               // 添加响应体内容
    void unmap();                                        // 释放 目标资源文件内存映射

public:
    void getClientIp(char *);

    // 设置当前 http 任务定时器
    void setTimer(http_conn *user, void(func)(http_conn *), time_t slot);

private:
    // 分配的资源
    int m_sockfd;       // http 任务对象的socket
    sockaddr_in m_addr; // 通信的socket地址

    char m_read_buf[READ_BUF_SIZE]; // 读缓冲
    int m_read_index;               // 当前读缓冲光标地址
    int m_checked_index;            // 当前需要解析的字符地址
    int m_start_line;               // 当前需要解析的请求行的首地址

    CHECK_STATE m_check_state; // 主状态机当前状态
    METHOD m_method;           // 请求方法

    char m_real_file[FILENAME_LEN]; // 客户请求的目标文件的完整路径，内容等于doc_root + m_url。
    char *m_url;                    // 请求目标文件的文件名
    char *m_version;                // 协议版本号，HTTP1.1
    char *m_host;                   // 主机名
    int m_content_length;           // 请求体字节大小
    bool m_linger;                  // http是否保持连接

    char m_write_buf[WRITE_BUF_SIZE]; // 写缓冲
    int m_write_index;                // 当前写缓冲光标地址
    char *m_file_addr;                // 客户请求的文件被mmap到内存中的地址
    struct stat m_file_stat;          // 客户请求的文件的目标状态,通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec m_iv[2];             // 采用writev来进行写回操作。从多个内存块进行写数据
    int m_iv_count;                   // 其中m_iv_count表示多个内存块的数量

    int bytes_to_send;   // 待发送数据大小
    int bytes_have_send; // 已发送数据大小
};

// 定时器节点类： 链表实现
class ulist_timer
{
public:
    // 节点类构造函数 （无参构造）
    ulist_timer() : prev(nullptr), next(nullptr), expire(-1),
                    func(nullptr), user_data(nullptr) {}

public:
    time_t expire;             // 到期时间 : 绝对时间
    void (*func)(http_conn *); // 回调处理函数
    http_conn *user_data;
    ulist_timer *prev;
    ulist_timer *next;
};

// 定时器链表  实现。 按到期时间 升序排列
class timer_list
{
public:
    // 定时器链表构造函数  (构造虚拟头尾节点)
    timer_list()
    {
        head = new ulist_timer();
        tail = new ulist_timer();
        head->next = tail;
        tail->prev = head;
    }

    // 定时器链表析构函数
    ~timer_list()
    {
        // 删除当前定时器链表所有节点
        while (head)
        {
            ulist_timer *t = head;
            head = head->next;
            delete t;
        }
    }

    // 添加 定时器节点 到 定时器链表
    void add_timer(ulist_timer *timer)
    {
        if (!timer)
            return;

        // 当前 链表 为空
        if (head->next == tail)
        {
            // 添加到 链表头部
            add_to_head(timer);
            printf("expire:%ld\n", timer->expire);
            printf("添加头节点成功~\n");
            printf("head->next : expire:%ld\n", head->next->expire);
            return;
        }

        // 否则插入到头结点以后的链表中
        printf("expire:%ld\n", timer->expire);
        printf("添加非头节点成功~\n");
        add_timer(timer, head);
    }

    // 更新当前定时器到正确的位置，只考虑过期时间延长，即往链表当前位置后移动
    void update_timer(ulist_timer *timer)
    {
        if (!timer)
            return;

        // 如果当前为最后一个有效定时器节点 或者 更新后的过期时间仍然比后继节点的过期时间小
        if (timer->next == tail || timer->expire < timer->next->expire)
        {
            return; // 无需更新
        }

        remove_node(timer);            // 摘除 timer
        add_timer(timer, timer->prev); // 重新在 timer->prev 之后插入合适的位置
    }

    void del_timer(ulist_timer *timer)
    {
        if (!timer)
            return;

        remove_node(timer); // 直接 摘除 timer
        // delete timer;       // 释放内存
    }

    // SIGALARM 信号每次被触发，就在其信号处理函数中 调用一次 tick() 函数。
    // 用以处理到期的链表任务
    void tick()
    {
        // print();
        // 链表空
        printf("调用 tick()... \n");
        if (head->next == tail)
            return;

        time_t curtime = time(NULL);   // 获取当前系统时间
        ulist_timer *cur = head->next; // 从 有效的开始节点进行扫描处理
        char ip[16];
        cur->user_data->getClientIp(ip);
        // 遍历到尾节点
        while (cur != tail)
        {
            if (!cur)
            {
                printf("有问题...\n");
                break;
            }
            // 最小到期时间 尚未到达， 直接返回
            if (curtime < cur->expire)
            {
                break;
            }
            printf("删除一个节点\n");
            ulist_timer *t = cur->next; // 临时保存后继
            cur->func(cur->user_data);  // 调用回调函数  执行任务对象到期处理函数
            cur = t;                    // 继续遍历后继节点
        }
    }

    void print()
    {
        printf("打印当前链表：\n");
        ulist_timer *p = head;
        while (p)
        {
            printf("到期时间：%ld\n", p->expire);
            p = p->next;
        }
    }

private:
    // 重载的辅助函数 将 timer 加入到 node 之后的链表中
    void add_timer(ulist_timer *timer, ulist_timer *node)
    {
        if (!timer)
            return;
        ulist_timer *cur = node->next; // 记录后继 进行插入
        while (cur != tail)
        {
            if (timer->expire < cur->expire)
            {
                add_one(timer, cur); // timer 添加到 cur 之前
                break;
            }
            cur = cur->next;
        }

        // 遍历到尾部 也没有插入，则直接插入尾部。当前过期时间 在链表中是最大的
        if (cur == tail)
        {
            add_one(timer, cur);
        }
    }

    // 将 timer 加入到 node 前面
    void add_one(ulist_timer *timer, ulist_timer *node)
    {
        if (!timer)
            return;
        timer->next = node;
        timer->prev = node->prev;
        node->prev->next = timer;
        node->prev = timer;
    }

    // 添加 timer 到 链表头部
    void add_to_head(ulist_timer *timer)
    {
        if (!timer)
            return;
        timer->next = head->next;
        timer->prev = head;
        head->next->prev = timer;
        head->next = timer;
    }

    // 将 timer 中 链表中摘除, 并返回 timer
    ulist_timer *remove_node(ulist_timer *timer)
    {
        if (timer)
        {
            timer->next->prev = timer->prev;
            timer->prev->next = timer->next;
            timer->next = nullptr;
            timer->prev = nullptr;
        }
        return timer;
    }

public:
    ulist_timer *head;
    ulist_timer *tail;
};

#endif