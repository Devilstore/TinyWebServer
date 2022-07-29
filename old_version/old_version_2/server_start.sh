
# 测试脚本
#!/bin/bash


CRTDIR=$(pwd)

if [ ! -d "${CRTDIR}/bin" ]; then
    mkdir -p ${CRTDIR}/bin
fi

# 编译
make
make clean

# 运行
./bin/webserver 6379
