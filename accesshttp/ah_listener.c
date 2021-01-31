/*
 * Description:
 *     History: yang@haipo.me, 2016/04/01, create
 */

#include "ah_listener.h"
#include "ah_config.h"

static nw_svr *listener_svr;
static nw_svr *monitor_svr;
static rpc_svr *worker_svr;

/* 1.listener服务 */

// 解包
static int listener_decode_pkg(nw_ses *ses, void *data, size_t max) {
  return max;
}
// 收包
static void listener_on_recv_pkg(nw_ses *ses, void *data, size_t size) {
  return;
}
// 错误
static void listener_on_error_msg(nw_ses *ses, const char *msg) {
  log_error("listener error, peer: %s, msg: %s",
            nw_sock_human_addr(&ses->peer_addr), msg);
}

// listener服务接收到客户端的信息时，转发给worker
static int listener_on_accept(nw_ses *ses, int sockfd, nw_addr_t *peer_addr) {

  log_info(" listener_on_accept 会话: %s -> %s ",
           nw_sock_human_addr(ses->host_addr),
           nw_sock_human_addr(&ses->peer_addr));

  log_info(" heartbeat_check : %d", worker_svr->heartbeat_check);
  log_info(" listener客户端地址: %s", nw_sock_human_addr(peer_addr));

  // 如果worker的rpc服务的底层网络会话数量是0，则worker没有连接到其他后台服务
  if (worker_svr->raw_svr->ses_count == 0) {
    log_error("no available worker");
    return -1;
  }

  //随机选择一个worker服务的网络会话id（这里的会话指worker用来连接其他后端服务的客户端）
  int worker = rand() % worker_svr->raw_svr->ses_count;

  log_info(" worker_svr的客户端会话数量: %d", worker_svr->raw_svr->ses_count);
  log_info(" 随机选择的worker: %d", worker);

  nw_ses *curr_test = worker_svr->raw_svr->ses_list_all;
  for (int i = 0; i < worker_svr->raw_svr->ses_count && curr_test; i++) {

    log_info("curr_test %s,%s", nw_sock_human_addr(curr_test->host_addr),
             nw_sock_human_addr(&curr_test->peer_addr));

    curr_test = curr_test->next;
  }

  // 指向第一个会话，并移动到最后一个会话（至少往后移一次）
  nw_ses *curr = worker_svr->raw_svr->ses_list_head;
  for (int i = 0; i < worker && curr; ++i) {
    curr = curr->next;
  }

  // 如果没有worker，抛出错误
  if (!curr) {
    log_error("choice worker fail");
    return -1;
  }

  log_info("worker当前会话 curr->id: %d, curr->sock_type: %d", curr->id,
           curr->sock_type);

  // worker的地址
  log_info("worker当前会话地址: %s -> %s", nw_sock_human_addr(curr->host_addr),
           nw_sock_human_addr(&curr->peer_addr));

  // 向worker的会话转发listener服务接收到的数据
  if (nw_ses_send_fd(curr, sockfd) < 0) {
    log_error(" listener向worker的会话发送数据失败 send sockfd fail: %s",
              strerror(errno));
    return -1;
  }

  // 关闭端口
  close(sockfd);
  return 0;
}

// listener服务初始化
static int init_listener_svr(void) {

  // 绑定事件
  nw_svr_type type;
  memset(&type, 0, sizeof(type));
  type.decode_pkg = listener_decode_pkg;
  type.on_accept = listener_on_accept;
  type.on_recv_pkg = listener_on_recv_pkg;
  type.on_error_msg = listener_on_error_msg;

  // 设置参数
  nw_svr_cfg *cfg = (nw_svr_cfg *)&settings.svr;

  // 创建listener的网络层服务
  listener_svr = nw_svr_create(cfg, &type, NULL);
  if (listener_svr == NULL)
    return -__LINE__;

  // 开始listener的网络层服务
  if (nw_svr_start(listener_svr) < 0)
    return -__LINE__;

  return 0;
}

/* 2.worker服务 */

// 接收到数据包，啥也不做
static void worker_on_recv_pkg(nw_ses *ses, rpc_pkg *pkg) {
  log_info("worker recv pkg");
  return;
}
// 接收到新连接，啥也不做
static void worker_on_new_connection(nw_ses *ses) {
  log_info("worker_new_cnnection, current worker number: %u,地址:%s -> %s",
           worker_svr->raw_svr->ses_count, nw_sock_human_addr(ses->host_addr),
           nw_sock_human_addr(&ses->peer_addr));
}
// 关闭新连接，啥也不做
static void worker_on_connection_close(nw_ses *ses) {
  log_info("worker close, current worker number: %u",
           worker_svr->raw_svr->ses_count - 1);
}

// worker rpc服务初始化
static int init_worker_svr(void) {

  // worker的rpc服务配置
  rpc_svr_cfg cfg;

  nw_svr_bind bind;
  memset(&cfg, 0, sizeof(cfg));

  cfg.name = strdup("ah_worker_rpc_svr");
  cfg.bind_count = 1;

  // 解析ah_config中的AH_LISTENER_BIND设置，得到绑定地址
  if (nw_sock_cfg_parse(AH_LISTENER_BIND, &bind.addr, &bind.sock_type) < 0) {
    log_error("init_worker_sver step 1 fail");
    return -__LINE__;
  }
  cfg.bind_arr = &bind;
  cfg.max_pkg_size = 1024;
  cfg.heartbeat_check = true;

  // worker的rpc服务事物，即核心业务逻辑
  rpc_svr_type type;
  type.on_recv_pkg = worker_on_recv_pkg;
  type.on_new_connection = worker_on_new_connection;
  type.on_connection_close = worker_on_connection_close;

  // 创建worker的rpc服务
  log_info("配置名称: %s", cfg.name);

  worker_svr = rpc_svr_create(&cfg, &type);
  if (worker_svr == NULL) {
    log_error("worker的rpc服务创建失败");
    return -__LINE__;
  }
  log_info(" name:%s rpc_svr 创建成功 : %s", worker_svr->name,
           nw_sock_human_addr(&cfg.bind_arr->addr));

  // 启动worker的rpc服务
  if (rpc_svr_start(worker_svr) < 0) {
    log_error("worker的rpc服务启动失败");
    return -__LINE__;
  }
  log_info("worker创建并启动成功");

  return 0;
}

static int monitor_decode_pkg(nw_ses *ses, void *data, size_t max) {
  return max;
}
static void monitor_on_recv_pkg(nw_ses *ses, void *data, size_t size) {
  return;
}

static int init_monitor_svr(void) {
  nw_svr_type type;
  memset(&type, 0, sizeof(type));
  type.decode_pkg = monitor_decode_pkg;
  type.on_recv_pkg = monitor_on_recv_pkg;

  monitor_svr = nw_svr_create(&settings.monitor, &type, NULL);
  if (monitor_svr == NULL)
    return -__LINE__;
  if (nw_svr_start(monitor_svr) < 0)
    return -__LINE__;

  return 0;
}

// listener服务，主要作用是
int init_listener(void) {
  int ret;

  // listener用来接收用户的http请求
  ret = init_listener_svr();
  if (ret < 0) {
    log_error("init_listener_svr fail");
    return ret;
  }

  // worker用来向其他程序提交或查询数据
  ret = init_worker_svr();
  if (ret < 0) {
    log_error("init_worker_svr fail");
    return ret;
  }
  log_info("name: %s ses_count: %d", worker_svr->name,
           worker_svr->raw_svr->ses_count);

  ret = init_monitor_svr();
  if (ret < 0) {
    log_error("init_monitor_svr fail");
    return ret;
  }

  return 0;
}
