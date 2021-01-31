/*
 * Description:
 *     History: yang@haipo.me, 2016/03/22, create
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "nw_clt.h"
#include "nw_log.h"

// 创建socket的客户端
static int create_socket(int family, int sock_type) {
  int sockfd = socket(family, sock_type, 0);
  if (sockfd < 0) {
    return -1;
  }
  if (nw_sock_set_nonblock(sockfd) < 0) {
    close(sockfd);
    return -1;
  }
  if (sock_type == SOCK_STREAM && (family == AF_INET || family == AF_INET6)) {
    if (nw_sock_set_no_delay(sockfd) < 0) {
      close(sockfd);
      return -1;
    }
  }

  return sockfd;
}

// 配置socket
static int set_socket_option(nw_clt *clt, int sockfd) {
  if (clt->read_mem > 0) {
    if (nw_sock_set_recv_buf(sockfd, clt->read_mem) < 0) {
      close(sockfd);
      return -1;
    }
  }
  if (clt->write_mem > 0) {
    if (nw_sock_set_send_buf(sockfd, clt->write_mem) < 0) {
      close(sockfd);
      return -1;
    }
  }

  return 0;
}

// 生成随机路径
static void generate_random_path(char *path, size_t size, char *prefix,
                                 char *suffix) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  srand(tv.tv_sec * tv.tv_usec);
  char randname[11];
  for (int i = 0; i < 10; ++i) {
    randname[i] = "a" + rand() % 26;
  }
  randname[10] = "\0";
  snprintf(path, size, "%s/%s%s%s", P_tmpdir, prefix, randname, suffix);
}

// 重联超时
static void on_reconnect_timeout(nw_timer *timer, void *privdata) {
  nw_clt *clt = (nw_clt *)privdata;
  nw_clt_start(clt);
}

// 重连
static void reconnect_later(nw_clt *clt) {
  nw_timer_set(&clt->timer, clt->reconnect_timeout, false, on_reconnect_timeout,
               clt);
  nw_timer_start(&clt->timer);
}

// 超时
static void on_connect_timeout(nw_timer *timer, void *privdata) {
  nw_clt *clt = (nw_clt *)privdata;
  if (!clt->on_connect_called) {
    nw_clt_close(clt);
    nw_clt_start(clt);
  }
}

// 监听连接
static void watch_connect(nw_clt *clt) {
  nw_timer_set(&clt->timer, clt->reconnect_timeout, false, on_connect_timeout,
               clt);
  nw_timer_start(&clt->timer);
}

// 接收到句柄
static void on_recv_fd(nw_ses *ses, int fd) { close(fd); }

// 关闭客户端
static int clt_close(nw_clt *clt) {
  //先停止计时器
  if (nw_timer_active(&clt->timer)) {
    nw_timer_stop(&clt->timer);
  }
  clt->connected = false;
  //然后关闭会话
  return nw_ses_close(&clt->ses);
}

// 连接事件
static void on_connect(nw_ses *ses, bool result) {

  log_info("nw_clt on_connect");
  //根据会话创建客户端
  nw_clt *clt = (nw_clt *)ses;

  clt->on_connect_called = true;

  if (result) { // 如果客户端已经启动并读到数据
    log_info("nw_clt connected and read result");
    clt->connected = true;
    set_socket_option(clt, clt->ses.sockfd);
    //从句柄中解析出协议，存入host_addr
    nw_sock_host_addr(ses->sockfd, ses->host_addr);
    if (clt->type.on_connect) {
      clt->type.on_connect(ses, result);
    }
  } else { // 如果客户端还没有启动，则关闭后重新启动
    log_info("nw_clt reconnect");
    if (clt->type.on_connect) {
      clt->type.on_connect(ses, result);
    }
    int ret = 0;
    if (clt->type.on_close) {
      ret = clt->type.on_close(&clt->ses);
    }
    clt_close(clt);
    if (ret > 0) {
      nw_clt_start(clt);
    } else {
      reconnect_later(clt);
    }
  }
}

// 异常事件
static void on_error(nw_ses *ses, const char *msg) {
  nw_clt *clt = (nw_clt *)ses;
  if (clt->type.on_error_msg) {
    clt->type.on_error_msg(ses, msg);
  }
  if (ses->sock_type == SOCK_DGRAM)
    return;
  int ret = 0;
  if (clt->type.on_close) {
    ret = clt->type.on_close(&clt->ses);
  }
  clt_close(clt);
  if (ret > 0) {
    nw_clt_start(clt);
  } else {
    reconnect_later(clt);
  }
}

// 关闭事件
static void on_close(nw_ses *ses) {
  nw_clt *clt = (nw_clt *)ses;
  int ret = 0;
  if (clt->type.on_close) {
    ret = clt->type.on_close(&clt->ses);
  }
  clt_close(clt);
  if (ret > 0) {
    nw_clt_start(clt);
  } else {
    reconnect_later(clt);
  }
}

// 创建客户端
nw_clt *nw_clt_create(nw_clt_cfg *cfg, nw_clt_type *type, void *privdata) {
  nw_loop_init();

  /* clt 设置 */

  // 网络客户端配置校验
  if (cfg->max_pkg_size == 0)
    return NULL;
  if (type->decode_pkg == NULL)
    return NULL;
  if (type->on_recv_pkg == NULL)
    return NULL;

  // 客户端分配内存
  nw_clt *clt = malloc(sizeof(nw_clt));
  memset(clt, 0, sizeof(nw_clt));

  clt->type = *type; //类型
  clt->reconnect_timeout =
      cfg->reconnect_timeout == 0 ? 1.0 : cfg->reconnect_timeout;

  if (cfg->buf_pool) { //缓冲
    clt->custom_buf_pool = true;
    clt->buf_pool = cfg->buf_pool;
  } else {
    clt->custom_buf_pool = false;
    clt->buf_pool = nw_buf_pool_create(cfg->max_pkg_size);
    if (clt->buf_pool == NULL) {
      nw_clt_release(clt);
      return NULL;
    }
  }
  clt->read_mem = cfg->read_mem;   //读内存
  clt->write_mem = cfg->write_mem; //写内存

  // 为本地地址分配内存
  nw_addr_t *host_addr = malloc(sizeof(nw_addr_t));
  if (host_addr == NULL) {
    nw_clt_release(clt);
    return NULL;
  }
  memset(host_addr, 0, sizeof(nw_addr_t));
  host_addr->family = cfg->addr.family;   // 类型
  host_addr->addrlen = cfg->addr.addrlen; // 地址长度

  /* ses 设置 */

  // 客户端会话的初始化
  if (nw_ses_init(&clt->ses, nw_default_loop, clt->buf_pool, cfg->buf_limit,
                  NW_SES_TYPE_CLIENT) < 0) {
    nw_clt_release(clt);
    return NULL;
  }
  // 将配置文件里的地址（即服务端地址）作为客户端会话的远方地址
  memcpy(&clt->ses.peer_addr, &cfg->addr, sizeof(nw_addr_t));

  // 将客户端会话的本地地址设置为host_addr
  clt->ses.host_addr = host_addr;
  clt->ses.sockfd = -1;
  clt->ses.sock_type = cfg->sock_type;
  clt->ses.privdata = privdata;

  clt->ses.decode_pkg = type->decode_pkg;
  clt->ses.on_recv_pkg = type->on_recv_pkg;
  clt->ses.on_recv_fd =
      type->on_recv_fd == NULL ? on_recv_fd : type->on_recv_fd;
  clt->ses.on_connect = on_connect;
  clt->ses.on_error = on_error;
  clt->ses.on_close = on_close;

  log_info(" nw_clt创建成功, peer_addr:%s",
           nw_sock_human_addr(&clt->ses.peer_addr));
  return clt;
}

// 启动客户端，即启动socket,并将socketfd句柄保存到客户端会话中
int nw_clt_start(nw_clt *clt) {
  log_info("nw_clt start");
  // 因为是客户端，所以采用远程地址即服务端一样的family类型
  int sockfd = create_socket(clt->ses.peer_addr.family, clt->ses.sock_type);

  if (sockfd < 0) {
    log_info("start fail 1");
    return -1;
  }

  clt->ses.sockfd = sockfd;

  // 如果socket是SOCK_DGRAM，即广播类型
  if (clt->ses.peer_addr.family == AF_UNIX &&
      clt->ses.sock_type == SOCK_DGRAM) {

    clt->ses.host_addr->un.sun_family = AF_UNIX;
    generate_random_path(clt->ses.host_addr->un.sun_path,
                         sizeof(clt->ses.host_addr->un.sun_path), "dgram",
                         ".sock");
    if (nw_ses_bind(&clt->ses, clt->ses.host_addr) < 0) {
      log_info("start fail 2");
      return -1;
    }
  }

  // 如果socket是SOCK_STREAM（tcp) 或 SOCK_SEQPACKET(sctp)
  if (clt->ses.sock_type == SOCK_STREAM ||
      clt->ses.sock_type == SOCK_SEQPACKET) {

    clt->connected = false;
    clt->on_connect_called = false;

    // 会话连接
    int ret = nw_ses_connect(&clt->ses, &clt->ses.peer_addr);
    if (ret < 0) {
      log_error(" nw_clt连接失败, peer_addr:%s",
                nw_sock_human_addr(&clt->ses.peer_addr));

      if (clt->type.on_close) {
        ret = clt->type.on_close(&clt->ses);
      }

      clt_close(clt); //关闭客户端
      if (ret > 0) {
        nw_clt_start(clt); //成功关闭时，马上重新启动（自迭代）
      } else {
        reconnect_later(clt); //否则延迟再启动
      }
    }

    // 执行on_connect_called事件
    if (!clt->on_connect_called) {
      watch_connect(clt);
    }
    log_info(" nw_clt启动成功, peer_addr:%s",
             nw_sock_human_addr(&clt->ses.peer_addr));

    return 0;
  } else {

    // SOCK_DGRAM广播类型时
    clt->connected = true;
    set_socket_option(clt, clt->ses.sockfd);
    nw_sock_host_addr(clt->ses.sockfd, clt->ses.host_addr);
    log_info("try nw_ses_start");
    // 会话启动
    return nw_ses_start(&clt->ses);
  }
}

// 关闭客户端
int nw_clt_close(nw_clt *clt) {
  //如果客户端有关闭事件，先执行关闭事件
  if (clt->type.on_close) {
    clt->type.on_close(&clt->ses);
  }
  // 然后关闭客户端
  return clt_close(clt);
}

// 释放客户端
void nw_clt_release(nw_clt *clt) {
  // 先释放会话
  nw_ses_release(&clt->ses);
  // 释放缓冲
  if (!clt->custom_buf_pool && clt->buf_pool) {
    nw_buf_pool_release(clt->buf_pool);
  }
  // 释放会话地址（即释放端口，防止一直占用）
  free(clt->ses.host_addr);
  // 释放客户端
  free(clt);
}

// 返回客户端的连接状态
bool nw_clt_connected(nw_clt *clt) { return clt->connected; }
