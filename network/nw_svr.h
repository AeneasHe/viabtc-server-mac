/*
 * Description:
 *     History: yang@haipo.me, 2016/03/20, create
 */

#ifndef _NW_SVR_H_
#define _NW_SVR_H_

#include <stdint.h>

#include "nw_buf.h"
#include "nw_evt.h"
#include "nw_ses.h"
#include "nw_sock.h"

/* nw_svr is a server object bind on multi addrs with same data process method
 */

// 绑定地址和类型
typedef struct nw_svr_bind {
  /* bind addr */
  nw_addr_t addr;
  /* bind type, SOCK_STREAM or SOCK_DGRAM or SOCK_SEQPACKET */
  int sock_type;
} nw_svr_bind;

// 服务的配置
typedef struct nw_svr_cfg {
  char *name;

  /* size of bind_arr */
  uint32_t bind_count;
  nw_svr_bind *bind_arr;

  /* max full message size */
  uint32_t max_pkg_size;
  /* nw_svr will keep a nw_buf_list for every stream connection to save
   * the pending send data. the buf_limit is the nw_buf_list limit */
  uint32_t buf_limit;

  /* will call nw_sock_set_recv_buf if not 0 */
  uint32_t read_mem;
  /* will call nw_sock_set_send_buf if not 0 */
  uint32_t write_mem;

} nw_svr_cfg;

// 服务的事物
typedef struct nw_svr_type {
  /* must
   *
   * for dgram and seqpacket connection, every package is consider as a
   * full message, but for stream connection, there is no boundary for
   * a message, use decode_pkg to determine the full message.
   *
   * return < 0: broken data, connection will be closed,
   * return = 0: don't contain a full message, nothing to do,
   * return > 0: return the size of a full message. */
  // 解析数据包
  int (*decode_pkg)(nw_ses *ses, void *data, size_t max);

  /* optional
   *
   * when accept a new connection for non dgram type svr, the default action
   * is add the connection to the server, you can overwrite this action by
   * set on_accept function. return < 0, sockfd will be close */
  int (*on_accept)(nw_ses *ses, int sockfd, nw_addr_t *peer_addr);

  /* optional
   *
   * called when a new connection is established */
  void (*on_new_connection)(nw_ses *ses);

  /* optional
   *
   * called when a connection is close */
  void (*on_connection_close)(nw_ses *ses);

  /* must
   *
   * called when a full message received, put your business logic here */
  void (*on_recv_pkg)(nw_ses *ses, void *data, size_t size);

  /* optional
   *
   * called when a fd is received, if not set, default action is to close it */
  void (*on_recv_fd)(nw_ses *ses, int fd);

  /* optional
   *
   * called when an error occur, msg is the detail of the error */
  void (*on_error_msg)(nw_ses *ses, const char *msg);

  /* optional
   *
   * if set, the on_privdata_free also should be set.
   * the return value will assign to nw_ses privdata
   * called when a new connection is established */
  void *(*on_privdata_alloc)(void *svr);

  /* optional
   *
   * called when on_privdata_alloc is set and the connection is closed */
  void (*on_privdata_free)(void *svr, void *privdata);
} nw_svr_type;

typedef struct nw_svr {
  char *name;

  uint32_t svr_count; // 服务端的数量,由配置中的bind地址数量决定
  nw_svr_type type;   // 服务端的各种响应事件

  nw_ses *ses_list_all;  // 全部会话列表
  nw_ses *ses_list_head; // 头部会话
  nw_ses *ses_list_tail; // 尾部会话
  uint32_t ses_count;    // 会话数量
  nw_cache *ses_cache;   // 会话缓存

  nw_buf_pool *buf_pool; // 缓冲池

  uint32_t buf_limit; // buf大小
  uint32_t read_mem;  // 读内存
  uint32_t write_mem; //写内存
  uint64_t id_start;  // 会话的初始id

  void *privdata; // 隐私数据
} nw_svr;

/* create a server instance, the privdata will assign to nw_svr privdata */
nw_svr *nw_svr_create(nw_svr_cfg *cfg, nw_svr_type *type,
                      void *privdata);         //创建服务端
int nw_svr_add_ses_by_fd(nw_svr *svr, int fd); //添加客户端句柄
int nw_svr_start(nw_svr *svr);                 //启动服务端
int nw_svr_stop(nw_svr *svr);                  //停止服务端
void nw_svr_release(nw_svr *svr); //释放服务端（会先调用停止服务端）
void nw_svr_close_ses(nw_svr *svr, nw_ses *ses); //关闭客户端

#endif
