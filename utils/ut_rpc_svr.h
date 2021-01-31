/*
 * Description:
 *     History: yang@haipo.me, 2016/03/29, create
 */

/*
 * rpc服务端：配置，事件，定义
 */

#ifndef _UT_RPC_SVR_H_
#define _UT_RPC_SVR_H_

#include "nw_buf.h"
#include "nw_svr.h"
#include "nw_timer.h"
#include "ut_rpc.h"

// rpc服务的配置
typedef struct rpc_svr_cfg {
  char *name;
  uint32_t bind_count;
  nw_svr_bind *bind_arr;
  uint32_t max_pkg_size;
  uint32_t buf_limit;
  uint32_t read_mem;
  uint32_t write_mem;
  bool heartbeat_check;
} rpc_svr_cfg;

// rpc服务的事件
typedef struct rpc_svr_type {
  void (*on_recv_pkg)(nw_ses *ses, rpc_pkg *pkg); //接收到数据包的回调
  void (*on_recv_fd)(nw_ses *ses, int fd);
  void (*on_new_connection)(nw_ses *ses);   //新连接的回调
  void (*on_connection_close)(nw_ses *ses); //连接关闭的回调
} rpc_svr_type;

// rpc服务的定义
typedef struct rpc_svr {
  char *name;
  nw_svr *raw_svr; // rpc服务的网络层服务端
  nw_timer timer;  //计时器
  nw_cache *privdata_cache;
  bool heartbeat_check;
  void (*on_recv_pkg)(nw_ses *ses, rpc_pkg *pkg);
  void (*on_new_connection)(nw_ses *ses);
} rpc_svr;

rpc_svr *rpc_svr_create(rpc_svr_cfg *cfg, rpc_svr_type *type); //创建rpc服务
int rpc_svr_start(rpc_svr *svr);                               //启动rpc服务
int rpc_svr_stop(rpc_svr *svr);                                //停止rpc服务
void rpc_svr_release(rpc_svr *svr);                            //释放rpc服务
void rpc_svr_close_ses(rpc_svr *svr, nw_ses *ses); // 关闭rpc服务的会话
rpc_svr *rpc_svr_from_ses(nw_ses *ses);

#endif
