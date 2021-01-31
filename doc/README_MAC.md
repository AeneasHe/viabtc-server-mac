# Mac 下安装编译指南

## 依赖

- zookeeper
  ```
  brew install zookeeper
  ```
- kafka  
   要先安装 java 1.8 即 jdk8，以下是先安装 adoptopen 社区版的 jdk8
  ```
  brew cask install adoptopenjdk/openjdk/adoptopenjdk8
  brew install kafka
  ```
- librdkafka
  ```
  brew install librdkafka
  ```
  源码安装要注释掉
  configure.self 里面的
  ```
  # mkl_check "libsasl2" disable
  # mkl_check "zstd" disable
  ```
- libev
  ```
  brew install libev
  ```
- libmpdec  
   需下载编译安装 [libmpdec](http://www.bytereef.org/mpdecimal/)
- jansson
  ```
  brew install jansson
  ```
- libmysqlclient-dev
  ```
  brew install mysql
  ```
- http_parser
  ```
  brew install  http-parser
  ```
- libcurl  
   需下载编译安装 [libcurl](https://curl.haxx.se/libcurl/)

## 编译

### 编译顺序

    先编译 network,utils
    然后编译 accesshttp,accessws
    接着编译 marketprice,matchengine
    最后编译 alertcenter,readhistory

### 编译错误

#### network

- EBADFD 错误  
   在 nw_sock.h 添加
  ```
  #ifndef EBADFD
      #define EBADFD EBADF
  #endif
  ```

#### utls

- fatal error: 'openssl/bio.h' file not found

  ```
  brew install openssl
  echo 'export PATH="/usr/local/opt/openssl/bin:$PATH"' >> ~/.bash_profile
  cd /usr/local/include
  ln -s ../opt/openssl/include/openssl
  ```

- fatal error: 'hiredis/hiredis.h' file not found  
   先编译安装 depends/hiredis

- fatal error:'endian.h' file not found 和 'byteswap.h' file not found  
   注释掉 ut_misc.h 以下两行

  ```c
  #include <endian.h>
  #include <byteswap.h>
  ```

  并添加

  ```c
  #ifdef __APPLE__
  #include <machine/endian.h>
  #include <libkern/OSByteOrder.h>

  #define htobe16(x) OSSwapHostToBigInt16(x)
  #define htole16(x) OSSwapHostToLittleInt16(x)
  #define be16toh(x) OSSwapBigToHostInt16(x)
  #define le16toh(x) OSSwapLittleToHostInt16(x)

  #define htobe32(x) OSSwapHostToBigInt32(x)
  #define htole32(x) OSSwapHostToLittleInt32(x)
  #define be32toh(x) OSSwapBigToHostInt32(x)
  #define le32toh(x) OSSwapLittleToHostInt32(x)

  #define htobe64(x) OSSwapHostToBigInt64(x)
  #define htole64(x) OSSwapHostToLittleInt64(x)
  #define be64toh(x) OSSwapBigToHostInt64(x)
  #define le64toh(x) OSSwapLittleToHostInt64(x)

  #define __BIG_ENDIAN BIG_ENDIAN
  #define __LITTLE_ENDIAN LITTLE_ENDIAN
  #define __BYTE_ORDER BYTE_ORDER
  #else
  #include
  #include
  #endif
  ```

- use of undeclared identifier 'program_invocation_short_name'  
   mac 下没有这个命令  
   在 ut_misc.h 里加入
  ```c
  #if defined(__APPLE__) || defined(__FreeBSD__)
  #define appname getprogname()
  #elif defined(_GNU_SOURCE)
  const char *appname = program_invocation_name;
  #else
  const char *appname = argv[0];
  #endif
  ```
  将 ut_misc.c 里的 program_invocation_short_name 改成 appname
- fatal error: 'mysql/mysql.h' file not found  
   将根目录下的 makefile.inc 添加以下值

  ```
  LDFLAGS= -L/usr/local/opt/mysql@5.7/lib
  CFLAGS= -I/usr/local/opt/mysql@5.7/include
  ```

- undefined reference to `rd_kafka_conf_set_log_cb'  
   makefile 里面将 LIBS 的 -lrdkafka 移到行尾

### accesshttp/accessws

- fatal error: 'error.h' file not found  
   ah_config.h 里将

  ```
  # include <error.h>
  ```

  改成

  ```
  #include <mach/error.h>
  ```

- ld: unknown option: -Bstatic  
   去掉 makefile 里的

  ```
  -Wl,-Bstatic
  -Wl,-Bdynamic
  ```

- Undefined symbols for architecture x86_64:"\_error", referenced from:  
   加入以下代码到相应 c 文件

  ```
  #ifdef __APPLE__
  #  define error printf
  #endif
  ```

- "\_clearenv", referenced from:  
   utils/ut_title.h 加入

  ```
  #if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
  #define clearenv() 0
  #endif
  ```

- library not found for -lssl  
   编译安装 openssl 到某个位置
  在 makefile 的 -lssl 前加入依赖库
  ``
  -L /Users/bitcocohe/Github/depends/lib

  ```

  ```

- mysql 库编译无法找到  
   在 makefile.inc 中的 LFAGS 加入 mysql 安装目录
  ```
  -L/usr/local/opt/mysql@5.7/lib
  LFLAGS  := -g -rdynamic -L/usr/local/opt/mysql@5.7/lib
  ```

### test/matchengine

- fatal error: 'error.h' file not found

## 启动

- zookeeper  
   服务启动：

  ```
  brew services start zookeeper
  ```

  临时启动：

  ```
  zkServer start
  ```

- kafka  
   服务启动：
  ```
  brew services start kafka
  ```
  临时启动：
  ```
  zookeeper-server-start /usr/local/etc/kafka/zookeeper.properties & kafka-server-start /usr/local/etc/kafka/server.properties
  ```
