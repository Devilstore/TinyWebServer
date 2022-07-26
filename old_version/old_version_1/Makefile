# makefile

TARGET := test
OBJS = main.o locker.o http_conn.o 
GCC = g++
CFLAGS = -Wall -pthread
TARGET := test


all : test clean

$(TARGET):$(OBJS)
	@$(GCC) $^ $(CFLAGS) -o $@
	@echo "ok. please input make run to test."

%.o : %.cpp
	@$(GCC) -c $^ -o $@

run :
	@echo "Default prot : 6379. \n"
	@echo [please input "http:your ip:6379/index.html" to access the website.]"\n"
	./test 6379

.PHONY : clean

clean:
	@$(RM) $(OBJS)


# makefile 一些参数

# AR ：归档维护程序的名称，默认值为ar
# CC ：C编译器的名称，默认值为C
# CXX ： C++编译器的名称，默认值为C++
# $@ ：规则的 目标文件名
# $< ：规则的 第一个依赖文件
# $^ ：规则的 所有依赖文件



# gcc的常用参数说明：

# -E 预处理指定的源文件，不进行编译。 			 生成 预处理后的源代码文件 .i 文件
# -S 编译指定的源文件，但是不进行汇编。 		 生成 汇编代码文件 .s 文件
# -c 编译，汇编指定的源代码，但是不进行链接。 	  生成 目标代码文件 .o 文件
# -o 表示指定生成的文件名
# -g 表示编译时候加入调试符号信息，debug时候需要这些信息，生成调试信息
# -I (大写i)表示设置头文件路径，-I./表示头文件路径为./
# -Wall 表示输出所有警告信息
# -w 不生成任何警告信息
# -D 表示设置宏定义,-DDEBUG表示编译debug版本,-DNDEBUG表示编译release版本， 可以在 -D 后面指定宏 进行调用
# -On 表示编译时候的优化级别，有4个级别-O0,-O1,-O2 -O3，-O0表示不优化,-O3表示最高优化级别
# -shared 			表示生成动态库
# -fPIC/fpic 		生成与位置无关的代码
# -std 				指定C方言，如 -std=99， gcc 默认方言是 GNU C
# -L 				指定库路径，如-L.表示库路径为当前目录
# -l (小写L)指定库名，如-lc表示引用libc.so



# g++的常用参数说明:

# -c 表示编译代码
# -o 表示指定生成的文件名
# -g 表示编译时候加入调试符号信息，debug时候需要这些信息
# -I (大写i)表示设置头文件路径，-I./表示头文件路径为./
# -Wall 表示输出所有警告信息
# -D 表示设置宏定义,-DDEBUG表示编译debug版本,-DNDEBUG表示编译release版本
# -On 表示编译时候的优化级别，有4个级别-O0,-O1,-O2 -O3，-O0表示不优化,-O3表示最高优化级别
# -shared 表示生成动态库
# -L 指定库路径，如-L.表示库路径为当前目录
# -l (小写L)指定库名，如-lc表示引用libc.so