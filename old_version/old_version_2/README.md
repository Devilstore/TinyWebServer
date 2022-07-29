# TinyWebServer 简介

本项目仅用于个人练习，2.0 版本

### Linux 下 C/C++轻量级 Web 服务器

- 使用 `线程池` + `非阻塞socket` + `epoll` + `事件处理（模拟Proactor）` 的并发模型
- 使用 `有限状态机` 解析 HTTP 请求报文，目前仅支持 **GET** 请求
- 实现 `同步/异步日志系统`，记录服务器的运行状态
- 使用 `链表结构` 来进行定时检测非活跃链接，并进行关闭处理
- 经过 `Webbench` 压力测试可以实现上万的并发请求

#### 1.日志系统的运行机制：

- 日志文件
  - 采用 C++11 的局部静态变量的懒汉模式获取单一实例
  - 生成日志文件，并根据 main.cpp 中的同步异步调用方式不同，采用不同的方式运行
- 同步
  - 需要判断是否需要进行分文件(按天,或者多个文件)写入日志
  - 直接格式化输出内容到缓冲区，并同步写入日志文件
- 异步
  - 需要判断是否需要进行分文件(按天,或者多个文件)写入日志
  - 格式化输出内容，并将日志内容加入到阻塞队列，异步写日志线程从阻塞队列获取到日志内容，进行写入日志文件。

#### 2.定时器定时检测非活跃链接机制：

- 定时方式
  - 采用 Linux 的 SIGALRM 信号进行定时
  - 针对每一个连接都存在一个定时器时间节点用于记录连接的相关信息
  - 定时器链表以升序排序

#### 操作系统： Linux

#### 运行：

依照如下步骤可以进行编译运行：

1. 克隆仓库(如果不可以，直接下载Zip进行解压也一样。)

   ```
   $ git clone git@github.com:Devilstore/TinyWebServer.git
   $ cd TinyWebServer/old_version/old_version_2
   ```

   

2. 构建并运行 (需要安装 make)        ----Linux 下安装 make `sudo apt install make`

   ```
   $ make
   $ make run
   ```
   
   或者使用下面的命令运行：

   ```
   $ sh server_start.sh
   ```
   
   
   
   注意：
   
   - 需要修改 http_conn.cpp 文件的 `doc_root` 为本地 `src` 目录
   - make run 默认端口设置 6379, 可自行使用 `./webserver port`运行 (port：自定义的端口号)， 或者修改 server_start.sh 中的  最后一行 6379 端口号
   
   

#### Webbench 压力测试

使用 webbench 对本项目进行压力测试

```
$ cd TinyWebServer/old_version/old_version_2/test_presure/webbench-1.5
$ make
$ ./webbench -c 10000 -t 5 http://192.168.31.73:6379/index.html
```

其中 `-c` 为客户端数量，`-t`为运行时间， 后面为 访问地址。

测试结果如下：

- 同步写日志

![image-20220728180350393](https://devil-picture-bed.oss-cn-shenzhen.aliyuncs.com/image/202207281803168.png)

- 异步写日志

![image-20220728180619969](https://devil-picture-bed.oss-cn-shenzhen.aliyuncs.com/image/202207281806003.png)
