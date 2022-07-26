
TARGET := test
OBJS = main.o locker.o http_conn.o 
GCC = g++
CFLAGS = -Wall -pthread

all : test clean

$(TARGET):$(OBJS)
	@$(GCC) $^ $(CFLAGS) -o $@
	@echo "ok. please input make run to test."

%.o : %.cpp
	@$(GCC) $^ -c -o $@

run :
	@echo "Default prot : 6379. \n"
	@echo [please input "http:your ip:6379/index.html" to access the website.]"\n"
	./test 6379

.PHONY : clean

clean:
	@$(RM) $(OBJS)


# g++的常用参数说明:

# -c 表示编译代码
# -o 表示指定生成的文件名
# -g 表示编译时候加入调试符号信息，debug时候需要这些信息
# -I (大写i)表示设置头文件路径，-I./表示头文件路径为./
# -Wall 表示输出所有警告信息
# -D 表示设置宏定义,-DDEBUG表示编译debug版本,-DNDEBUG表示编译release版本
# -O 表示编译时候的优化级别，有4个级别-O0,-O1,-O2 -O3，-O0表示不优化,-O3表示最高优化级别
# -shared 表示生成动态库
# -L 指定库路径，如-L.表示库路径为当前目录
# -l (小写L)指定库名，如-lc表示引用libc.so