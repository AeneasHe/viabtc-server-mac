/*
 * Description:
 *     History: yang@haipo.me, 2016/03/20, create
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "nw_log.h"
#include "nw_svr.h"

// 创建socket的服务端
static int create_socket(int family, int sock_type) {

  //创建socket
  // sockfd指 socket file description, 套接字连接的文件句柄（描述符）
  int sockfd = socket(family, sock_type, 0);
  if (sockfd < 0) {
    log_info(" family:%d sock_type:%d", family, sock_type);
    log_info(" create_socket 1 fail");
    return -1;
  }

  // 设置socket为非阻塞
  if (nw_sock_set_nonblock(sockfd) < 0) {
    close(sockfd);
    log_info(" create_socket 2 fail");
    return -1;
  }

  // 设置socket复用地址
  if (nw_sock_set_reuse_addr(sockfd) < 0) {
    close(sockfd);
    log_info(" create_socket 3 fail");
    return -1;
  }
  if (sock_type == SOCK_STREAM && (family == AF_INET || family == AF_INET6)) {

    //设置socket无延迟
    if (nw_sock_set_no_delay(sockfd) < 0) {
      close(sockfd);
      log_info(" create_socket 4 fail");
      return -1;
    }
  }

  return sockfd;
}

// 配置socket连接
static int set_socket_option(nw_svr *svr, int sockfd) {
  if (svr->read_mem > 0) {
    // 设置接收缓冲
    if (nw_sock_set_recv_buf(sockfd, svr->read_mem) < 0) {
      close(sockfd);
      return -1;
    }
  }
  if (svr->write_mem > 0) {
    // 设置发送缓冲
    if (nw_sock_set_send_buf(sockfd, svr->write_mem) < 0) {
      close(sockfd);
      return -1;
    }
  }

  return 0;
}

// 事物：异常
static void on_error(nw_ses *ses, const char *msg) {
  nw_svr *svr = (nw_svr *)ses->svr;
  if (svr->type.on_error_msg) {
    svr->type.on_error_msg(ses, msg);
  }
  if (ses->ses_type == NW_SES_TYPE_COMMON) {
    nw_svr_close_ses(svr, ses);
  }
}
// 事物：关闭
static void on_close(nw_ses *ses) {
  nw_svr *svr = (nw_svr *)ses->svr;
  if (ses->ses_type == NW_SES_TYPE_COMMON) {
    nw_svr_close_ses(svr, ses);
  }
}

// 事物：接收到
static void on_recv_fd(nw_ses *ses, int fd) { close(fd); }

// 释放服务器
static void nw_svr_free(nw_svr *svr) {
  if (svr->buf_pool)
    nw_buf_pool_release(svr->buf_pool);
  if (svr->ses_cache)
    nw_cache_release(svr->ses_cache);
  if (svr->ses_list_all) {
    for (uint32_t i = 0; i < svr->svr_count; ++i) {
      if (svr->ses_list_all[i].write_buf != NULL) {
        nw_ses_release(&svr->ses_list_all[i]);
        free(svr->ses_list_all[i].host_addr);
      }
    }
    free(svr->ses_list_all);
  }
  free(svr);
}

// 向服务器添加新会话
static int nw_svr_add_ses(nw_ses *ses, int sockfd, nw_addr_t *peer_addr) {

  //指向session的服务器
  nw_svr *svr = (nw_svr *)ses->svr;

  log_info("name: %s nw_svr 开始添加会话", svr->name);

  // 设置socket配置
  set_socket_option(svr, sockfd);
  if (nw_sock_set_nonblock(sockfd) < 0) {
    return -1;
  }

  void *privdata = NULL;
  if (svr->type.on_privdata_alloc) {
    privdata = svr->type.on_privdata_alloc(svr);
    if (privdata == NULL) {
      return -1;
    }
  }

  // 为新会话分配内存
  nw_ses *new_ses = nw_cache_alloc(svr->ses_cache);
  if (new_ses == NULL) {
    return -1;
  }
  memset(new_ses, 0, sizeof(nw_ses));

  // 初始化新session
  if (nw_ses_init(new_ses, nw_default_loop, svr->buf_pool, svr->buf_limit,
                  NW_SES_TYPE_COMMON) < 0) {
    nw_cache_free(svr->ses_cache, new_ses);
    if (privdata) {
      svr->type.on_privdata_free(svr, privdata);
    }
    return -1;
  }

  //新session绑定参数
  memcpy(&new_ses->peer_addr, peer_addr, sizeof(nw_addr_t));
  new_ses->host_addr = ses->host_addr;
  new_ses->sockfd = sockfd;
  new_ses->sock_type = ses->sock_type;
  new_ses->privdata = privdata;
  new_ses->svr = svr;

  new_ses->id = svr->id_start++;
  if (new_ses->id == 0)
    new_ses->id = svr->id_start++;

  // 新session设置事物
  new_ses->decode_pkg = svr->type.decode_pkg;
  new_ses->on_recv_pkg = svr->type.on_recv_pkg;
  new_ses->on_recv_fd =
      svr->type.on_recv_fd == NULL ? on_recv_fd : svr->type.on_recv_fd;
  new_ses->on_error = on_error;
  new_ses->on_close = on_close;

  // 将新session放到服务的尾部
  if (svr->ses_list_tail) {
    log_info("追加 session to svr tail, clt地址: %s -> %s",
             nw_sock_human_addr(new_ses->host_addr),
             nw_sock_human_addr(&new_ses->peer_addr));
    new_ses->prev = svr->ses_list_tail;
    svr->ses_list_tail->next = new_ses;
    svr->ses_list_tail = new_ses;
    new_ses->next = NULL;
  } else {
    log_info("添加 session to svr tail, clt地址: %s -> %s",
             nw_sock_human_addr(new_ses->host_addr),
             nw_sock_human_addr(&new_ses->peer_addr));
    svr->ses_list_head = new_ses;
    svr->ses_list_tail = new_ses;
    new_ses->prev = NULL;
    new_ses->next = NULL;
  }

  // 更新客户端数量
  svr->ses_count++;

  // 启动客户端的会话
  nw_ses_start(new_ses);

  //如果服务器设置了新连接事物，就执行该事物
  if (svr->type.on_new_connection) {
    svr->type.on_new_connection(new_ses);
  }

  return 0;
}

// 事物：网络层接收到socket消息
// ses 会话，还没有初始化和启动，通过nw_svr_add_ses进行初始化并绑定到svr上
// sockfd 句柄
// peer_addr 远程地址
static int on_accept(nw_ses *ses, int sockfd, nw_addr_t *peer_addr) {

  nw_svr *svr = (nw_svr *)ses->svr;

  log_info("\n\tname: %s on_accept接收到消息 %s, %s, %s ", svr->name,
           nw_sock_human_addr(ses->host_addr),
           nw_sock_human_addr(&ses->peer_addr), nw_sock_human_addr(peer_addr));

  //如果会话的服务器已经绑定了接收事件，则执行接收事件
  if (svr->type.on_accept) {
    log_info(" 执行上层的on_accpet事物");
    return svr->type.on_accept(ses, sockfd, peer_addr);
  }

  log_info("name: %s 开始给服务器添加新会话", svr->name);
  // 否则给服务器添加新会话
  return nw_svr_add_ses(ses, sockfd, peer_addr);
}

// 服务器接收到远程发送来的fd时，根据fd创建与远程客户端的会话
int nw_svr_add_ses_by_fd(nw_svr *svr, int fd) {
  log_info("nw_svr_add_ses_by_fd");
  nw_addr_t peer_addr;
  if (nw_sock_peer_addr(fd, &peer_addr) < 0) {
    return -1;
  }
  nw_ses *ses = NULL;
  for (uint32_t i = 0; i < svr->svr_count; ++i) {
    if (peer_addr.family == svr->ses_list_all[i].host_addr->family) {
      ses = &svr->ses_list_all[i];
      break;
    }
  }
  if (ses == NULL)
    return -1;
  return nw_svr_add_ses(ses, fd, &peer_addr);
}

// 创建服务器
nw_svr *nw_svr_create(nw_svr_cfg *cfg, nw_svr_type *type, void *privdata) {
  nw_loop_init();

  // 配置参数检查
  if (cfg->name == NULL) {
    log_error(" nw_svr_cfg.name should not NULL");
    return NULL;
  }

  if (cfg->bind_count == 0) {
    log_error(" nw_svr_cfg.bind_count should great than 0");
    return NULL;
  }
  if (cfg->max_pkg_size == 0) {
    log_info(" nw_svr_cfg.max_pkg_size should great than 0");
    return NULL;
  }

  // 类型参数检查
  if (type->decode_pkg == NULL) {
    log_info(" create step 3 fail");
    return NULL;
  }
  if (type->on_recv_pkg == NULL) {
    log_info(" create step 4 fail");
    return NULL;
  }
  if (type->on_privdata_alloc && !type->on_privdata_free) {
    log_info(" create step 5 fail");
    return NULL;
  }

  /* svr 相关设置 */

  // 给服务端分配内存并初始化
  nw_svr *svr = malloc(sizeof(nw_svr));
  if (svr == NULL) {
    log_info(" create step 6 fail");
    return NULL;
  }
  memset(svr, 0, sizeof(nw_svr));

  // 绑定设置和类型参数
  svr->name = cfg->name;
  svr->type = *type;                //类型
  svr->svr_count = cfg->bind_count; //根据配置中的bind地址数量解析出来的个数

  // 所有的会话列表
  // 服务端有几个进程，就先创建服务端进程之间的会话
  svr->ses_list_all = malloc(sizeof(nw_ses) * svr->svr_count);

  if (svr->ses_list_all == NULL) {
    nw_svr_free(svr);
    log_info(" 服务端会话列表分配内存失败");
    return NULL;
  }

  // 缓冲池
  svr->buf_pool = nw_buf_pool_create(cfg->max_pkg_size);
  if (svr->buf_pool == NULL) {
    nw_svr_free(svr);
    log_info(" create step 8 fail");
    return NULL;
  }

  // 会话缓存
  svr->ses_cache = nw_cache_create(sizeof(nw_ses));
  if (svr->ses_cache == NULL) {
    nw_svr_free(svr);
    log_info(" create step 9 fail");
    return NULL;
  }

  svr->buf_limit = cfg->buf_limit;
  svr->read_mem = cfg->read_mem;
  svr->write_mem = cfg->write_mem;
  svr->privdata = privdata;

  // 服务端的所有会话列表初始化
  // 先初始化内存
  memset(svr->ses_list_all, 0, sizeof(nw_ses) * svr->svr_count);

  // 循环创建会话，并绑定到服务端上
  for (uint32_t i = 0; i < svr->svr_count; ++i) {

    /* ses 会话相关设置 */

    // 指向第i个会话
    nw_ses *ses = &svr->ses_list_all[i];

    // 创建会话的socket连接
    int sockfd =
        create_socket(cfg->bind_arr[i].addr.family, cfg->bind_arr[i].sock_type);
    if (sockfd < 0) {
      nw_svr_free(svr);
      log_info(" create step 10 fail");
      return NULL;
    }

    // 将刚才的socket绑定地址保存到host_addr
    nw_addr_t *host_addr = malloc(sizeof(nw_addr_t));
    if (host_addr == NULL) {
      nw_svr_free(svr);
      log_error(" host_addr分配内存失败");
      return NULL;
    }
    memcpy(host_addr, &cfg->bind_arr[i].addr, sizeof(nw_addr_t));

    // 会话初始化
    if (nw_ses_init(ses, nw_default_loop, svr->buf_pool, svr->buf_limit,
                    NW_SES_TYPE_SERVER) < 0) {
      free(host_addr);
      nw_svr_free(svr);
      log_error(" nw_ses_init fail");
      return NULL;
    }

    // 会话设置参数
    ses->sockfd = sockfd;                        //绑socket定连接
    ses->sock_type = cfg->bind_arr[i].sock_type; //绑定socket类型
    // 会话的本地地址
    ses->host_addr = host_addr; //绑定本地地址
    ses->svr = svr;             // 绑定服务

    // 会话设置事件
    ses->on_accept = on_accept;           // 会话事物：接收
    ses->decode_pkg = type->decode_pkg;   // 会话事物：解析数据包
    ses->on_recv_pkg = type->on_recv_pkg; // 会话事物：接收到数据包
    ses->on_recv_fd = type->on_recv_fd == NULL
                          ? on_recv_fd
                          : type->on_recv_fd; // 会话事物：接收到fd
    ses->on_error = on_error;                 // 会话事物：错误
    ses->on_close = on_close;                 // 会话事物：关闭

    // 如果是广播型套接字SOCK_DGRAM
    if (cfg->bind_arr[i].sock_type == SOCK_DGRAM) {
      ses->peer_addr.family = host_addr->family;
      ses->peer_addr.addrlen = host_addr->addrlen;
      set_socket_option(svr, sockfd);
    }

    log_info(" name: %s nw_svr 会话创建成功，id:%d, host_addr：%s", svr->name,
             i, nw_sock_human_addr(ses->host_addr));
  }

  log_info(" name: %s nw_svr创建成功", svr->name);
  return svr;
}

// 启动服务器
int nw_svr_start(nw_svr *svr) {

  // svr_count：服务进程数
  for (uint32_t i = 0; i < svr->svr_count; ++i) {

    // 取出所有会话
    nw_ses *ses = &svr->ses_list_all[i];

    // 绑定会话的本机地址到socket上
    if (nw_ses_bind(ses, ses->host_addr) < 0) {
      return -1;
    }

    // 启动会话，主要作用是开始监听消息
    if (nw_ses_start(ses) < 0) {
      return -1;
    }
    log_info(" name: %s nw_svr 启动成功 host_addr:%s", svr->name,
             nw_sock_human_addr(ses->host_addr));
  }

  return 0;
}

// 停止服务器
int nw_svr_stop(nw_svr *svr) {
  for (uint32_t i = 0; i < svr->svr_count; ++i) {
    // 关闭session
    if (nw_ses_stop(&svr->ses_list_all[i]) < 0) {
      return -1;
    }
  }
  return 0;
}

// 释放服务器
void nw_svr_release(nw_svr *svr) {
  nw_svr_stop(svr);
  nw_ses *curr = svr->ses_list_head;
  while (curr) {

    nw_ses *next = curr->next;

    //关闭当前session
    if (svr->type.on_connection_close) {
      svr->type.on_connection_close(curr);
    }
    // 释放当前session的privdata
    if (curr->privdata) {
      svr->type.on_privdata_free(svr, curr->privdata);
    }
    // 释放session
    nw_ses_release(curr);
    // 释放缓存
    nw_cache_free(svr->ses_cache, curr);

    // 处理下一个session
    curr = next;
  }
  for (uint32_t i = 0; i < svr->svr_count; ++i) {
    nw_ses_release(&svr->ses_list_all[i]);
  }
  nw_cache_release(svr->ses_cache);
  nw_svr_free(svr);
}

// 服务器关闭客户端
void nw_svr_close_ses(nw_svr *svr, nw_ses *ses) {
  if (ses->id == 0)
    return;
  if (ses->ses_type != NW_SES_TYPE_COMMON)
    return;

  // 先执行连接关闭事物
  if (svr->type.on_connection_close) {
    svr->type.on_connection_close(ses);
  }

  /* 从session链中断开该session */

  //处理上一个session
  if (ses->prev) {
    ses->prev->next =
        ses->next; //将上一个session的next指针指向该session的下一个session即可
  } else {
    svr->ses_list_head =
        ses->next; //如果没有上一个session,则将服务的头部session指向下一个session
  }
  //处理下一个session
  if (ses->next) {
    ses->next->prev = ses->prev;
  } else {
    svr->ses_list_tail = ses->prev;
  }

  // 释放session的隐私数据
  if (ses->privdata) {
    svr->type.on_privdata_free(svr, ses->privdata);
  }
  // 释放session
  nw_ses_release(ses);

  // 释放session缓存
  nw_cache_free(svr->ses_cache, ses);

  // 服务的客户端数量减1
  svr->ses_count--;
}
