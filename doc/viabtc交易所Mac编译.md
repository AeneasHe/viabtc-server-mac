openssl 位置

/usr/local/opt/openssl

注意修改

makefile.inc 文件里 mysql 的实际位置

各文件夹里的 makefile 的 openssl 的实际位置

https://github.com/viabtc/viabtc_exchange_server

ubuntu 安装依赖时 aptitude install 比 apt install 包更全

【依赖文件】

zookeeper

brew install zookeeper

kafka

#要先安装 java 1.8 即 jdk8，以下是安装 adoptopen 社区版的 jdk8

brew cask install adoptopenjdk/openjdk/adoptopenjdk8

brew install kafka

librdkafka

    	brew install librdkafka

源码安装要注释掉

configure.self 里面的

# mkl_check "libsasl2" disable

# mkl_check "zstd" disable

libev

brew install libev

libmpdec

需下载编译安装

jansson

brew install jansson

libmysqlclient-dev (apt-get install libmysqlclient-dev)

brew install mysql

http_parser

brew install http-parser

libcurl

需下载编译安装

【启动】

zookeeper

服务启动：brew services start zookeeper

临时启动：zkServer start

kafka

服务启动：brew services start kafka

临时启动： zookeeper-server-start /usr/local/etc/kafka/zookeeper.properties & kafka-server-start /usr/local/etc/kafka/server.properties

【mac 编译错误】

network

EBADFD 错误

在 nw_sock.h 添加

#ifndef EBADFD

    #define EBADFD EBADF

#endif

utls

fatal error: 'openssl/bio.h' file not found

brew install openssl

echo 'export PATH="/usr/local/opt/openssl/bin:$PATH"' >> ~/.bash_profile

cd /usr/local/include
ln -s ../opt/openssl/include/openssl

fatal error: 'hiredis/hiredis.h' file not found

先编译安装 depends/hiredis

fatal error:

'endian.h' file not found

'byteswap.h' file not foun

注释掉 ut_misc.h 以下两行

#include <endian.h>

#include <byteswap.h>

并添加

#ifdef **APPLE**

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

#define \_\_BIG_ENDIAN BIG_ENDIAN

#define \_\_LITTLE_ENDIAN LITTLE_ENDIAN

#define \_\_BYTE_ORDER BYTE_ORDER

#else

#include

#include

#endif

use of undeclared identifier 'program_invocation_short_name'

mac 下没有这个命令

在 ut_misc.h 里加入

#if defined(**APPLE**) || defined(**FreeBSD**)

#define appname getprogname()

#elif defined(\_GNU_SOURCE)

const char \*appname = program_invocation_name;

#else

const char \*appname = argv[0];

#endif

将 ut_misc.c 里的 program_invocation_short_name 改成 appname

fatal error: 'mysql/mysql.h' file not found

将根目录下的 makefile.inc 添加以下值

LDFLAGS= -L/usr/local/opt/mysql@5.7/lib

CFLAGS= -I/usr/local/opt/mysql@5.7/include

undefined reference to `rd_kafka_conf_set_log_cb'

makefile 里面将 LIBS 的 -lrdkafka 移到行尾

accesshttp/accessws

fatal error: 'error.h' file not found

ah_config.h 里将# include <error.h>改成#include <mach/error.h>

ld: unknown option: -Bstatic

去掉 makefile 里的

-Wl,-Bstatic

-Wl,-Bdynamic

Undefined symbols for architecture x86_64:

"\_error", referenced from:

加入以下代码到相应 c 文件

#ifdef **APPLE**

# define error printf

#endif

"\_clearenv", referenced from

utils/ut_title.h 加入 s

#if defined(**FreeBSD**) || defined(**OpenBSD**) || defined(**NetBSD**) || defined(**APPLE**)

#define clearenv() 0

#endif

library not found for -lssl

编译安装 openssl 到某个位置

在 makefile 的 -lssl 前加入依赖库

-L /Users/bitcocohe/Github/depends/lib

mysql 库编译无法找到

在 makefile.inc 中的 LFAGS 加入 mysql 安装目录-L/usr/local/opt/mysql@5.7/lib

LFLAGS := -g -rdynamic -L/usr/local/opt/mysql@5.7/lib

【安装参考 1】

#make a new folder and cd into it before you run these cmds

apt-get update
apt-get install -y wget vim psmisc git
apt-get install -y libev-dev libmpdec-dev libmysqlclient-dev libssl-dev
apt-get install -y build-essential autoconf libtool python

# clear

rm -rf /var/lib/apt/lists/\*

#install jansson
git clone https://github.com/akheron/jansson
cd ./jansson
autoreconf -i
./configure
make
make install
cd ../

# install kafka

wget --no-check-certificate https://codeload.github.com/edenhill/librdkafka/tar.gz/v0.11.3 -O librdkafka.tar.gz
tar xzvf librdkafka.tar.gz
rm -rf librdkafka.tar.gz

cd ./librdkafka-\*
./configure --prefix=/usr/local
sed -i "s/WITH_LDS=/#WITH_LDS=/g" Makefile.config
make
make install
cd ../

# install curl

wget --no-check-certificate https://codeload.github.com/curl/curl/tar.gz/curl-7_45_0 -O curl-7.45.0.tar.gz
tar xzvf curl-7.45.0.tar.gz
rm -rf curl-7.45.0.tar.gz
mv curl-\* curl
cd ./curl
./buildconf
./configure --prefix=/usr/local --disable-ldap --disable-ldaps

#./configure --prefix=/usr/local --disable-shared --enable-static --without-libidn --without-librtmp --without-gnutls --without-nss --without-libssh2 --without-zlib --without-winidn --disable-rtsp --disable-ldap --disable-ldaps --disable-ipv6 --without-ssl

make
make install
cd ../

# install liblz4

apt-get update  
apt-get install -y liblz4-dev

# download viabtc

git clone https://github.com/Bringer-of-Light/viabtc_exchange_server.git
mv viabtc_exchange_server viabtc

cd ./viabtc
make -C depends/hiredis clean
make -C network clean
make -C utils clean
make -C accesshttp clean
make -C accessws clean
make -C matchengine clean
make -C marketprice clean
make -C alertcenter clean
make -C readhistory clean

make -C depends/hiredis
make -C depends/hiredis install
make -C network
make -C utils
make -C accesshttp
make -C accessws
make -C matchengine
make -C marketprice
make -C alertcenter
make -C readhistory
cd ../

# copy all exe file into bin

mkdir bin
cp -f accesshttp/accesshttp.exe bin
cp -f accessws/accessws.exe bin
cp -f matchengine/matchengine.exe bin
cp -f marketprice/marketprice.exe bin
cp -f alertcenter/alertcenter.exe bin
cp -f readhistory/readhistory.exe bin
ll

【安装参考 2】

compile viabtc_exchange_server on Ubuntu 16.04

Felix021  发表于 2018 年 03 月 26 日 21:29 | Hits: 2090

Tag: Blockchain

If you find this article helpful, you may like to donate to my ETH address:

0x84D5084a0142a26081a2d06F3505cfc2CDaE9009

Detailed guide to compile viabtc_exchange_server on Ubuntu 16.04

## DEPENDENCIES ## 

引用

$ sudo apt install -y libev-dev libjansson-dev libmpdec-dev libmysqlclient-dev libcurl4-gnutls-dev libldap2-dev libgss-dev librtmp-dev libsasl2-dev

# librdkafka: 0.11.3+; DO NOT INSTALL BY APT: version too old (0.8.x); 

# if you do, remove them by: sudo apt remove librdkafka1 librdkafka-dev 

$ wgethttps://github.com/edenhill/librdkafka/archive/v0.11.3.tar.gz-O librdkafka-0.11.3.tar.gz

$ tar zxf librdkafka-0.11.3.tar.gz

$ cd librdkafka-0.11.3

$ ./configure

$ make

$ sudo make install

## COMPILATION ## 

引用

$ git clonehttps://github.com/viabtc/viabtc_exchange_server.git

$ cd viabtc_exchange_server

$ make -C depends/hiredis

$ make -C network

$ vi utils/makefile #modify INCS

# INCS = -I ../network -I ../depends 

$ make -C utils

$ vi accesshttp/makefile #modify INCS & LIBS

# INCS = -I ../network -I ../utils -I ../depends 

# LIBS = -L ../utils -lutils -L ../network -lnetwork -L ../depends/hiredis -Wl,-Bstatic -lev -ljansson -lmpdec -lrdkafka -lz -lssl -lcrypto -lhiredis -lcurl -Wl,-Bdynamic -lm -lpthread -ldl -lssl -lldap -llber -lgss -lgnutls -lidn -lnettle -lrtmp -lsasl2 -lmysqlclient 

$ make -C accesshttp

$ vi accessws/makefile

{modify INCS and LIBS like accesshttp/makefile}

$ make -C accessws

vi alertcenter/makefile

{modify INCS and LIBS like accesshttp/makefile}

$ make -C alertcenter

$ vi marketprice/makefile

{modify INCS and LIBS like accesshttp/makefile}

$ make -C marketprice

$ vi matchengine/makefile

{modify INCS and LIBS like accesshttp/makefile}

$ make -C matchengine

$ vi readhistory/makefile

{modify INCS and LIBS like accesshttp/makefile}

$ make -C readhistory
