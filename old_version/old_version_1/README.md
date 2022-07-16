# TinyWebServer简介

## 

本项目仅用于个人练习，基础版本。

### Linux下C++轻量级Web服务器

- 使用 `线程池` + `非阻塞socket`  + `epoll` + `事件处理（模拟Proactor）` 的并发模型
- 使用 `有限状态机` 解析HTTP请求报文，目前仅支持 **GET** 请求
- 经过  `Webbench` 压力测试可以实现上万的并发请求



#### 操作系统： Linux



#### 运行

依照如下步骤可以进行编译运行：

1. 克隆仓库

   ```
   $ git clone git@github.com:Devilstore/TinyWebServer.git
   $ cd TinyWebServer/old_version/old_version_1
   ```

2. 构建并运行 

   ```
   $ g++ *.cpp -pthread
   $ ./a.out port
   ```

   注意：需要修改http_conn.cpp 文件的 `doc_root` 为本地 `src` 目录



#### Webbench 压力测试

使用 webbench 对本项目进行压力测试

```
$ cd TinyWebServer/old_version/old_version_1/test_presure/webbench-1.5
$ make
$ ./webbench -c 10000 -t 5 http://192.168.31.73:6379/index.html
```

其中 `-c` 为客户端数量，`-t`为运行时间， 后面为 访问地址。



测试结果如下：

![image-20220716183557747](https://devil-picture-bed.oss-cn-shenzhen.aliyuncs.com/image/202207161835808.png)