#include "http_conn.h"

// 定义 HTTP 相应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";

// 网站根目录
const char *doc_root = "/home/devil/linux/web1/src";

// 类静态变量成员 初始化
int http_conn::m_epfd = -1;     // 所有socket事件都被注册到同一个epoll对象中
int http_conn::m_user_size = 0; // 统计当前用户数量
int *http_conn::pipefd = new int[2];
timer_list *http_conn::timer_lst = new timer_list();

// 为fd设置非阻塞属性
int setnonblocking(int fd)
{
    int oldflag = fcntl(fd, F_GETFL);
    int newflag = oldflag | O_NONBLOCK;
    fcntl(fd, F_SETFL, newflag);
    return oldflag;
}

// 向 epfd 添加需要监听的 fd
void addfd(int epfd, int fd, bool one_shot)
{
    epoll_event event; // epoll 存储的数据类型
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP; // 监听 读就绪事件。 EPOLLRDHUP，kernel 2.6.17 连接断开会返回HUP

    // oneshot 开启，需要注册 EPOLLONESHOT，来使得每次只会有一个线程/进程对该fd操作
    if (one_shot)
    {
        event.events |= EPOLLONESHOT; // 防止 多个线程 对同一 socket 进行操作
    }
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);

    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

// 从epoll中移除 正在监听 的文件描述符
void removefd(int epfd, int fd)
{
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改文件描述符，重置socketfd上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能被触发
void modifyfd(int epfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP; // 每次oneshot触发之后都需要重新修改注册
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event);              // 修改 fd epoll属性
}

// 定时回调函数 信号调用处理函数
// 移除 epoll 注册事件，在 链表中 移除该节点，关闭http连接
void back_func(http_conn *user_data)
{
    // 移除 epoll 注册事件
    removefd(user_data->m_epfd, user_data->m_sockfd);
    // 在 链表中 移除该节点
    http_conn::timer_lst->del_timer(user_data->m_timer);
    // 关闭http连接
    user_data->close_conn();
}

// 关闭通信连接
void http_conn::close_conn()
{
    // 关闭该 fd
    if (m_sockfd != -1)
    {
        removefd(m_epfd, m_sockfd); // 从epfd 中 删除 fd
        m_sockfd = -1;              // 重置 fd 为-1
        --m_user_size;              // 总用户数量 - 1
    }

    // 关闭定时器链接
    if (m_timer)
    {
        delete m_timer;
        m_timer = nullptr;
    }
}

// 初始化新接收的 用户连接任务请求。（将用户连接信息都封装在 http 任务类内）
void http_conn::init(int sockfd, const sockaddr_in &addr)
{
    m_sockfd = sockfd;
    m_addr = addr;
    // m_timer = new ulist_timer();

    // 设置 通信 socket 端口复用，1 表示端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 加入 epoll 对象中
    addfd(m_epfd, sockfd, true); // oneshot设置真，每次只通知一次
    ++m_user_size;               // 总用户数量 + 1
    init();                      // 初始化相关信息
}

// 初始化 分析请求状态相关信息
void http_conn::init()
{
    bytes_to_send = 0;
    bytes_have_send = 0;

    m_check_state = CHECK_STATE_REQUESTLINE; // 主状态机  初始状态为：解析请求首行
    m_linger = false;                        // 默认不保持连接
    m_method = GET;                          // 默认请求方式为GET
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;

    m_start_line = 0;    // 当前需要解析的 请求行索引地址
    m_checked_index = 0; // 当前需要解析的字符地址
    m_read_index = 0;    // 当前读缓冲光标地址
    m_write_index = 0;   // 当前写缓冲光标地址

    bzero(m_read_buf, READ_BUF_SIZE);   // 清空读缓冲
    bzero(m_write_buf, WRITE_BUF_SIZE); // 清空写缓冲
    bzero(m_real_file, FILENAME_LEN);   // 清空 资源文件名
}

// 循环读取客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read()
{
    if (m_read_index >= READ_BUF_SIZE)
    {
        return false; // 读缓冲溢出
    }
    int bytes_read = 0; // 临时保存 recv 每次读取的字节大小
    // 循环读取 socket 通信数据
    while (true)
    {
        // 从 socket 中读取数据
        bytes_read = recv(m_sockfd, m_read_buf + m_read_index, READ_BUF_SIZE - m_read_index, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN | errno == EWOULDBLOCK)
            { // 有 无数据 的通知，退出循环  // 没有数据
                break;
            }
            return false; // 否则， 读取错误 返回假
        }
        else if (bytes_read == 0)
        { // 对方关闭连接
            return false;
        }
        // 读取成功
        m_read_index += bytes_read;
    }
    // printf("读取到的数据:\n%s\n", m_read_buf);
    return true;
}

// 解析一行数据  判断依据 \r\n。遇到 \r\n 替换为 \0\0，取地址字符串就会自动分开
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp; // 扫描字符
    for (; m_checked_index < m_read_index; ++m_checked_index)
    {
        temp = m_read_buf[m_checked_index];
        // 遇到 \r\n 就会返回，所以每次只会解析一行。 m_checked_index为下一行首地址
        if (temp == '\r')
        {
            if ((m_checked_index + 1) == m_read_index)
            {
                return LINE_OPEN; // 返回数据不完整
            }
            else if (m_read_buf[m_checked_index + 1] == '\n')
            {
                m_read_buf[m_checked_index++] = '\0'; // \r 置为字符串结束符。
                m_read_buf[m_checked_index++] = '\0'; // \n 置为字符串结束符。
                return LINE_OK;                       //  当前行解析成功
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if ((m_checked_index > 1) && (m_read_buf[m_checked_index - 1] == '\r'))
            {
                m_read_buf[m_checked_index - 1] = '\0'; // \r 置为字符串结束符。
                m_read_buf[m_checked_index++] = '\0';   // \n 置为字符串结束符。
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

// 解析请求首行, 获取请求方法，目标URL，HTTP版本
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // "GET /index.html HTTP/1.1"
    m_url = strpbrk(text, " \t"); // 找第一个空格或者\t在text出现的位置

    // 如果不存在 直接返回非法
    if (!m_url)
    {
        return BAD_REQUEST;
    }

    // 置位空字符，字符串结束符
    *m_url++ = '\0'; // "GET\0/index.html HTTP/1.1", m_url++ 指向 /index.html HTTP/1.1

    char *method = text; // 即 GET
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else
    {
        return BAD_REQUEST; // 否则为错误请求
    }

    // 检索字符串 str1 中出现的第一个 str2 中字符的下标。
    m_version = strpbrk(m_url, " \t"); // "/index.html HTTP/1.1"
    if (!m_version)
    {
        return BAD_REQUEST;
    }

    *m_version++ = '\0'; // "/index.html\0HTTP/1.1"  m_version++ 指向HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    // strcasecmp 忽略大小写的 字符串比较  如果是 "http://192.168.30.128:10000/index.html" 形式
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;                 // m_url = 192.168.30.128:10000/index.html
        m_url = strchr(m_url, '/'); // m_url = /index.html
    }

    if (!m_url || m_url[0] != '/') // url不合法, m_url 为资源文件的相对路径
    {
        return BAD_REQUEST;
    }

    // 请求行解析完毕，更新主状态机 状态
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST; // 解析完请求首行，需要继续解析请求体
}

// 解析请求头部的信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    //遇到空行，表示请求头部字段解析完毕
    if (text[0] == '\0')
    {
        // 如果HTTP请求有请求内容，还需要读取m_content_length字节的请求内容
        // 主状态机转移到 CHECK_STATE_CONTENT
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST; // 表示请求尚不完整
        }
        // 否则 说明已经得到一个完整的HTTP请求
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        // 处理 Connection 字段。  Connection:keep-alive
        text += 11;
        text += strspn(text, " \t"); // 跳过 空格和\t
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true; // 保持连接
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        // 处理 Content-Length 字段。
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text); // 将字符串 转长整型。
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        // 处理 Host 字段。
        text += 5;
        text += strspn(text, " \t"); // 跳过 空格和\t
        m_host = text;
    }
    else
    {
        // printf("oop! unknow header.\n");
    }
    return NO_REQUEST; // 处理完头部 返回 NO_REQUEST 继续进行解析
}

// 解析请求体  只判断了 数据是否被读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    // 当前 读缓冲区下一个位置 大于 内容 + 当前检查字符 说明 content 被完整读入 读缓冲区
    if (m_read_index >= (m_content_length + m_checked_index))
    {
        text[m_content_length] = '\0'; // 截断 content后面的内容
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 主状态机 ： 解析HTTP请求
http_conn::HTTP_CODE http_conn::process_read()
{

    LINE_STATUS line_status = LINE_OK; // 初始为LINE_OK
    HTTP_CODE ret = NO_REQUEST;        // 初始为 NO_REQUEST

    char *text = 0;

    // 遍历所有 http 行
    while ((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK) ||
           (line_status = parse_line()) == LINE_OK)
    {
        // 情况1. 解析到了请求体，也是完整的数据。
        // 情况2. 解析到了一行完整的数据。
        // 操作. 获取一行数据
        text = get_line();
        m_start_line = m_checked_index; // 更新下一行首地址
        // printf("got 1 http line: %s\n", text);

        // 主状态机
        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text); // 解析请求首行
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST; // 语法错误直接结束
            }
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text); // 解析请求头部
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST; // 语法错误直接结束
            }
            else if (ret == GET_REQUEST)
            {
                return do_request(); // 获得一个完整的客户请求，则去调用解析函数
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text); // 解析请求体
            if (ret == GET_REQUEST)
            {
                return do_request(); // 获得一个完整的客户请求，则去调用解析函数
            }
            line_status = LINE_OPEN; // 未获得完整请求，置从状态为 LINE_OPEN（行数据不完整）
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

// 解析请求 做出响应
// 当得到一个完整正确的HTTP请求时候，我们需要分析目标文件的属性
// 如果目标文件存在，对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
http_conn::HTTP_CODE http_conn::do_request()
{
    // "home/devil/webserver/src"
    strcpy(m_real_file, doc_root); // 根目录 拷贝进 目标文件完整路径
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1); // 把url拼接到目录下得到完整路径

    // 获取m_real_file文件的相关的状态信息 传到m_file_stat参数， 返回值 -1失败 0成功
    if (stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE; // 不存在资源
    }

    // 判断访问权限  读权限  S_IROTH
    if (!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否为目录
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }

    // 以只读方式打开资源文件
    int fd = open(m_real_file, O_RDONLY);
    // 内存映射  只读， 写入时，会产生映射文件的拷贝
    m_file_addr = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);           // 关闭fd
    return FILE_REQUEST; // 获取资源文件成功
}

// 对内存映射区 进行一个 释放, 执行 unmap()
void http_conn::unmap()
{
    if (m_file_addr)
    {
        munmap(m_file_addr, m_file_stat.st_size);
        m_file_addr = 0;
    }
}

// 写HTTP响应  一次性写入所有 sockfd 数据。写完 返回真
bool http_conn::write()
{
    int temp = 0;

    if (bytes_to_send == 0)
    {
        // 待发送数据大小为0，本次响应结束
        modifyfd(m_epfd, m_sockfd, EPOLLIN); // 修改 为监听读就绪事件，等待下次读事件触发
        init();                              // 响应结束，初始化 请求链接
        return true;
    }

    // 循环 写数据到sockfd
    while (true)
    {
        // writev 分散写  从 m_iv指定的多块内存中写数据到 sockfd
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1)
        {
            // 如果TCP写缓冲没有空间，则等待下一轮的EPOLLOUT事件，
            // 虽然在此期间服务器无法立即接收同一客户的下一请求，但可以保证连接的完整性。
            if (errno == EAGAIN)
            {
                modifyfd(m_epfd, m_sockfd, EPOLLOUT); // 修改 epoll 对象属性
                return true;
            }
            unmap(); // 写数据结束，释放内存映射
            return false;
        }
        bytes_have_send += temp; // 已发送字节数更新
        bytes_to_send -= temp;   // 待发送字节数更新

        // 如果当前 第一块数据区发送完毕 更新 m_iv 资源块数据
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            // 更新第二块数据块 基址  // 已发送字节减去第一块数据长度  m_wirte_index 即为 响应首行+响应头部
            m_iv[1].iov_base = m_file_addr + (bytes_have_send - m_write_index);
            m_iv[1].iov_len = bytes_to_send; // 剩余待发送字节为 当前 数据区长度
        }
        else
        {
            // 第一块数据还没有读取完毕， 更新第一块数据
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0)
        {
            // 写数据完毕  ，释放内存映射
            unmap();
            modifyfd(m_epfd, m_sockfd, EPOLLIN); // 修改 epoll 对象属性
            if (m_linger)
            {
                init(); // 如果连接 保持，则初始化链接，用于下次使用
                return true;
            }
            else
            {
                printf("断开连接\n");
                return false;
            }
        }
    }
}

// 向写缓冲区写入待发送的数据
bool http_conn::add_response(const char *format, ...)
{
    if (m_write_index >= WRITE_BUF_SIZE)
    {
        return false; // 写缓冲溢出
    }

    va_list arg_list;
    va_start(arg_list, format); // 可变参数列表
    // 写入 写缓冲
    int len = vsnprintf(m_write_buf + m_write_index, WRITE_BUF_SIZE - 1 - m_write_index, format, arg_list);
    if (len >= (WRITE_BUF_SIZE - 1 - m_write_index))
    {
        return false; // 数据过多 写溢出。
    }
    m_write_index += len; // 当前写缓冲下标更新
    va_end(arg_list);
    return true;
}

// 添加 响应 请求首行
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 添加响应 请求头部
bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
    // return true;
}

// 添加响应头部信息 : Content-Length
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

// 添加响应头部信息 : Content-Type
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

// 添加响应头部信息 : Connection
bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

// 添加响应头部信息 : 空行
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

// 添加响应体内容
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

// 根据服务器解析HTTP请求的结果，决定生成响应给客户的内容
bool http_conn::process_write(HTTP_CODE read_ret)
{
    switch (read_ret)
    {
        // 内部错误
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
        {
            return false;
        }
        break;
    }
        // 错误请求
    case BAD_REQUEST:
    {
        add_status_line(400, error_400_title);
        add_headers(strlen(error_400_form));
        if (!add_content(error_400_form))
        {
            return false;
        }
        break;
    }
        // 资源不存在
    case NO_RESOURCE:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
        {
            return false;
        }
        break;
    }
        // 禁止访问
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
        {
            return false;
        }
        break;
    }
        // 获取资源文件成功
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        add_headers(m_file_stat.st_size);

        // 更新 写数据src资源块信息  [请求首行 + 请求头部, 请求体], 等待 写就绪事件
        m_iv[0].iov_base = m_write_buf;
        m_iv[0].iov_len = m_write_index;
        m_iv[1].iov_base = m_file_addr;
        m_iv[1].iov_len = m_file_stat.st_size;
        m_iv_count = 2;

        bytes_to_send = m_write_index + m_file_stat.st_size; // 待发送数据总大小为两部分之和
        return true;
    }
    default:
        return false;
    }
    // m_iv[0].iov_base = m_write_buf;
    // m_iv[0].iov_len = m_write_index;
    // m_iv_count = 1;
    // bytes_to_send = m_write_index;
    // return true;
}

// 工作函数 : 处理客户端请求入口函数，由线程池中的工作线程调用。
void http_conn::process()
{
    printf("正在处理http请求>>>\n");
    // 解析http请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {                                        // 如果客户数据不足，继续接受数据
        modifyfd(m_epfd, m_sockfd, EPOLLIN); // 修改epoll通知获取数据, 继续进行读取数据
        return;                              // 结束处理 程序
    }

    printf("正在生成http响应>>>\n");
    // 生成http响应
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modifyfd(m_epfd, m_sockfd, EPOLLOUT); // 生成响应完毕，写入 epoll 对象，通知 EPOLLOUT
}

// 获取当前 http 连接源 IP
void http_conn::getClientIp(char *ip)
{
    inet_ntop(AF_INET, &m_addr.sin_addr.s_addr, ip, 16);
}

// 设置当前 http 任务定时器
void http_conn::setTimer(http_conn *user, void(func)(http_conn *), time_t slot)
{
    bool flag = false;
    if (m_timer == NULL)
    {
        flag = true;
        m_timer = new ulist_timer();
    }
    m_timer->user_data = user;
    m_timer->func = back_func;
    time_t cur = time(NULL);
    printf("当前时间：%ld\n", cur);
    m_timer->expire = cur + 3 * slot;
    if (flag)
    {
        printf("调用添加\n");
        http_conn::timer_lst->add_timer(m_timer);
    }
    else
    {
        printf("调用修改\n");
        http_conn::timer_lst->update_timer(m_timer);
    }
    printf("当前客户端到期时间：%ld\n", m_timer->expire);
}