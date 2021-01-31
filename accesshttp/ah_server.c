/*
 * Description:
 *     History: yang@haipo.me, 2017/04/21, create
 */

#include "ah_server.h"
#include "ah_config.h"

static http_svr *svr;
static nw_state *state;
static dict_t *methods;
static rpc_clt *worker;

// 注释：clt是client的缩写

static rpc_clt *matchengine; // matchengine的rpc客户端
static rpc_clt *marketprice; // marketprice的rpc客户端
static rpc_clt *readhistory; // readhistory的rpc客户端

// 状态信息
struct state_info {
  nw_ses *ses;
  uint64_t ses_id;
  int64_t request_id;
};

// 后端服务包括：matchengine,marketprice,readhistory三个服务
// 向后端服务转发的请求信息
struct request_info {
  rpc_clt *clt; // 上面的后端服务之一的rpc客户端
  uint32_t cmd; // rpc指令指令
};

// 响应错误
static void reply_error(nw_ses *ses, int64_t id, int code, const char *message,
                        uint32_t status) {
  json_t *error = json_object();
  json_object_set_new(error, "code", json_integer(code));
  json_object_set_new(error, "message", json_string(message));
  json_t *reply = json_object();
  json_object_set_new(reply, "error", error);
  json_object_set_new(reply, "result", json_null());
  json_object_set_new(reply, "id", json_integer(id));

  char *reply_str = json_dumps(reply, 0);
  send_http_response_simple(ses, status, reply_str, strlen(reply_str));
  free(reply_str);
  json_decref(reply);
}

// 响应错误请求
static void reply_bad_request(nw_ses *ses) {
  send_http_response_simple(ses, 400, NULL, 0);
}

// 响应内部错误
static void reply_internal_error(nw_ses *ses) {
  send_http_response_simple(ses, 500, NULL, 0);
}

// 响应方法没找到
static void reply_not_found(nw_ses *ses, int64_t id) {
  reply_error(ses, id, 4, "method not found", 404);
}

// 响应超时
static void reply_time_out(nw_ses *ses, int64_t id) {
  reply_error(ses, id, 5, "service timeout", 504);
}

// 入口： http服务接收到用户的请求时，相应处理
static int on_http_request(nw_ses *ses, http_request_t *request) {

  // 记录新请求日志
  log_info("new http request, url: %s, method: %u", request->url,
           request->method);

  // 检查请求参数
  if (request->method != HTTP_POST || !request->body) {
    reply_bad_request(ses);
    return -__LINE__;
  }

  // 拿到请求的消息体
  json_t *body = json_loadb(request->body, sdslen(request->body), 0, NULL);
  if (body == NULL) {
    goto decode_error;
  }

  // 从消息体中拿到各参数
  json_t *id = json_object_get(body, "id"); //请求id
  if (!id || !json_is_integer(id)) {
    goto decode_error;
  }
  json_t *method = json_object_get(body, "method"); //请求方法
  if (!method || !json_is_string(method)) {
    goto decode_error;
  }
  json_t *params = json_object_get(body, "params"); //请求参数
  if (!params || !json_is_array(params)) {
    goto decode_error;
  }

  //记录请求内容日志
  log_info("from: %s body: %s", nw_sock_human_addr(&ses->peer_addr),
           request->body);

  //根据请求方法查找对应的rpc接口
  dict_entry *entry = dict_find(methods, json_string_value(method));
  if (entry == NULL) {
    //没有找到方法时，响应reply_not_found
    log_error("没有找到对应的rpc方法");
    reply_not_found(ses, json_integer_value(id));
  } else {

    // 找到请求的方法信息
    struct request_info *req = entry->val; //请求的信息

    // 如果请求没有连接
    if (!rpc_clt_connected(req->clt)) {
      log_error("内部错误，rpc方法对应的转发链路没有连接");
      reply_internal_error(ses);
      json_decref(body);
      return 0;
    }

    nw_state_entry *entry = nw_state_add(state, settings.timeout, 0); //
    struct state_info *info = entry->data;
    info->ses = ses; //后面的ses是用户的连接会话
    info->ses_id = ses->id;
    info->request_id = json_integer_value(id);

    rpc_pkg pkg; // rpc请求的数据包
    memset(&pkg, 0, sizeof(pkg));
    pkg.pkg_type = RPC_PKG_TYPE_REQUEST;
    pkg.command = req->cmd;
    pkg.sequence = entry->id;
    pkg.req_id = json_integer_value(id);
    pkg.body = json_dumps(params, 0);
    pkg.body_size = strlen(pkg.body);

    rpc_clt_send(req->clt, &pkg); //调用rpc接口，向用户发送数据包

    log_info("send request to %s, cmd: %u, sequence: %u",
             nw_sock_human_addr(rpc_clt_peer_addr(req->clt)), pkg.command,
             pkg.sequence);
    free(pkg.body);
  }

  json_decref(body);
  return 0;

decode_error:
  if (body)
    json_decref(body);
  sds hex = hexdump(request->body, sdslen(request->body));
  log_fatal("peer: %s, decode request fail, request body: \n%s",
            nw_sock_human_addr(&ses->peer_addr), hex);
  sdsfree(hex);
  reply_bad_request(ses);
  return -__LINE__;
}

/* 以下是字典的各种方法 */

// hash
static uint32_t dict_hash_func(const void *key) {
  return dict_generic_hash_function(key, strlen(key));
}

// 键的比较
static int dict_key_compare(const void *key1, const void *key2) {
  return strcmp(key1, key2);
}

// 键重复
static void *dict_key_dup(const void *key) { return strdup(key); }

static void dict_key_free(void *key) { free(key); }

static void *dict_val_dup(const void *val) {
  struct request_info *obj = malloc(sizeof(struct request_info));
  memcpy(obj, val, sizeof(struct request_info));
  return obj;
}

static void dict_val_free(void *val) { free(val); }

/* 以下是各种事物的处理 */

// 事物：超时
static void on_state_timeout(nw_state_entry *entry) {
  log_error("state id: %u timeout", entry->id);
  struct state_info *info = entry->data;
  if (info->ses->id == info->ses_id) {
    reply_time_out(info->ses, info->request_id);
  }
}
// 事物：后端连接成功
static void on_backend_connect(nw_ses *ses, bool result) {
  rpc_clt *clt = ses->privdata;
  if (result) {
    log_info("name: %s 连接成功 peer_addr: %s", clt->name,
             nw_sock_human_addr(ses->host_addr),
             nw_sock_human_addr(&ses->peer_addr));

  } else {
    log_error("name: %s 连接失败 peer_addr: %s", clt->name,
              nw_sock_human_addr(ses->host_addr),
              nw_sock_human_addr(&ses->peer_addr));
  }
}
// 事物：后端接收到消息
static void on_backend_recv_pkg(nw_ses *ses, rpc_pkg *pkg) {
  log_debug("recv pkg from: %s, cmd: %u, sequence: %u",
            nw_sock_human_addr(&ses->peer_addr), pkg->command, pkg->sequence);
  nw_state_entry *entry = nw_state_get(state, pkg->sequence);
  if (entry) {
    struct state_info *info = entry->data;
    if (info->ses->id == info->ses_id) {
      log_info("send response to: %s",
               nw_sock_human_addr(&info->ses->peer_addr));
      send_http_response_simple(info->ses, 200, pkg->body, pkg->body_size);
    }
    nw_state_del(state, pkg->sequence);
  }
}

// 事物：worker的rpc客户端建立连接时
static void on_worker_connect(nw_ses *ses, bool result) {
  log_info("worker rpc_clt 开始连接");
  if (result) {
    log_info("worker rpc_clt 连接成功 地址: %s",
             nw_sock_human_addr(&ses->peer_addr));

  } else {
    log_error("worker rpc_clt 连接失败 地址: %s",
              nw_sock_human_addr(&ses->peer_addr));
  }
}

// 事物：worker的rpc客户端接收到消息时,啥也不做
static void on_worker_recv_pkg(nw_ses *ses, rpc_pkg *pkg) {
  log_info("worker_clt on_recv_pkg");
  return;
}

// 事物：worker的rpc客户端接收到会话句柄时
static void on_worker_recv_fd(nw_ses *ses, int fd) {

  log_info("worker的rpc客户端接收到句柄, fd:%d", fd);
  // 将该句柄，添加到http服务的底层网络服务的会话列表中
  if (nw_svr_add_ses_by_fd(svr->raw_svr, fd) < 0) {
    log_error("http_server 根据句柄fd添加会话失败, fd:%d, error: %s", fd,
              strerror(errno));
    close(fd);
  }
  log_info("http_server 根据句柄fd添加会话成功, fd:%d", fd);
}

// http服务用于连接worker的rpc客户端
static int init_woker_clt(void) {

  log_info("worker rpc_clt 开始启动");
  /* worker的rpc客户端配置 */
  rpc_clt_cfg cfg;

  nw_addr_t addr; //地址
  memset(&cfg, 0, sizeof(cfg));
  cfg.name = strdup("ah_worker_clt");
  cfg.addr_count = 1;
  cfg.addr_arr = &addr;

  if (nw_sock_cfg_parse(AH_LISTENER_BIND, &addr, &cfg.sock_type) < 0) {
    log_error("init_woker_clt fail 1");
    return -__LINE__;
  }
  cfg.max_pkg_size = 1024;

  /* worker的rpc客户端事物 */
  rpc_clt_type type;
  memset(&type, 0, sizeof(type));
  type.on_connect = on_worker_connect;
  type.on_recv_pkg = on_worker_recv_pkg;
  type.on_recv_fd = on_worker_recv_fd;

  //创建连接worker的rpc客户端
  worker = rpc_clt_create(&cfg, &type);
  if (worker == NULL) {
    log_error("ah_worker_clt 创建失败");
    return -__LINE__;
  }
  log_info("name: %s rpc_clt 创建成功", worker->name);
  // 启动连接worker的rpc客户端
  if (rpc_clt_start(worker) < 0) {
    log_error("ah_worker_clt 启动失败");
    return -__LINE__;
  }
  log_info("name: %s rpc_clt 启动成功", worker->name);

  return 0;
}
//添加handler
static int add_handler(char *method, rpc_clt *clt, uint32_t cmd) {
  // method方法字符串
  // 后端rpc服务的客户端
  // 后端rpc服务的命令
  struct request_info info = {.clt = clt, .cmd = cmd};
  if (dict_add(methods, method, &info) == NULL)
    return __LINE__;
  return 0;
}

//绑定http消息的rpc处理方法
static int init_methods_handler(void) {

  // 资产
  ERR_RET_LN(add_handler("asset.list", matchengine, CMD_ASSET_LIST));
  ERR_RET_LN(add_handler("asset.summary", matchengine, CMD_ASSET_SUMMARY));

  // 余额
  ERR_RET_LN(add_handler("balance.query", matchengine, CMD_BALANCE_QUERY));
  ERR_RET_LN(add_handler("balance.update", matchengine, CMD_BALANCE_UPDATE));
  ERR_RET_LN(add_handler("balance.history", readhistory, CMD_BALANCE_HISTORY));

  //订单
  ERR_RET_LN(add_handler("order.put_limit", matchengine, CMD_ORDER_PUT_LIMIT));
  ERR_RET_LN(
      add_handler("order.put_market", matchengine, CMD_ORDER_PUT_MARKET));

  ERR_RET_LN(add_handler("order.cancel", matchengine, CMD_ORDER_CANCEL));
  ERR_RET_LN(add_handler("order.book", matchengine, CMD_ORDER_BOOK));
  ERR_RET_LN(add_handler("order.depth", matchengine, CMD_ORDER_BOOK_DEPTH));
  ERR_RET_LN(add_handler("order.pending", matchengine, CMD_ORDER_QUERY));
  ERR_RET_LN(
      add_handler("order.pending_detail", matchengine, CMD_ORDER_DETAIL));

  ERR_RET_LN(add_handler("order.deals", readhistory, CMD_ORDER_DEALS));
  ERR_RET_LN(add_handler("order.finished", readhistory, CMD_ORDER_HISTORY));
  ERR_RET_LN(add_handler("order.finished_detail", readhistory,
                         CMD_ORDER_DETAIL_FINISHED));

  //市场行情
  ERR_RET_LN(add_handler("market.last", marketprice, CMD_MARKET_LAST));
  ERR_RET_LN(add_handler("market.deals", marketprice, CMD_MARKET_DEALS));
  ERR_RET_LN(add_handler("market.kline", marketprice, CMD_MARKET_KLINE));
  ERR_RET_LN(add_handler("market.status", marketprice, CMD_MARKET_STATUS));
  ERR_RET_LN(
      add_handler("market.status_today", marketprice, CMD_MARKET_STATUS_TODAY));
  ERR_RET_LN(
      add_handler("market.user_deals", readhistory, CMD_MARKET_USER_DEALS));
  ERR_RET_LN(add_handler("market.list", matchengine, CMD_MARKET_LIST));
  ERR_RET_LN(add_handler("market.summary", matchengine, CMD_MARKET_SUMMARY));

  return 0;
}

// 这里的server主要指：http_server 及附属的rpc客户端，worker客户端
int init_server(void) {
  log_info("ah_server 开始启动");

  /* 1. 定义methods */

  // 声明dt并初始化
  dict_types dt;
  memset(&dt, 0, sizeof(dt));

  // 绑定字典类型的方法
  dt.hash_function = dict_hash_func;
  dt.key_compare = dict_key_compare;
  dt.key_dup = dict_key_dup;
  dt.val_dup = dict_val_dup;
  dt.key_destructor = dict_key_free;
  dt.val_destructor = dict_val_free;

  methods = dict_create(&dt, 64);

  if (methods == NULL) {
    log_error("init_server fail 1");
    return -__LINE__;
  }

  /* 2. 定义网络状态state */

  nw_state_type st;
  memset(&st, 0, sizeof(st));
  st.on_timeout = on_state_timeout;
  state = nw_state_create(&st, sizeof(struct state_info));
  if (state == NULL) {
    log_error("init_server fail 2");
    return -__LINE__;
  }

  /* 3. 定义rpc客户端ct, 用来连接后台其他进程 */

  // rpc客户端: 用来连接matchengine,marketprice,readhistory
  rpc_clt_type ct;
  memset(&ct, 0, sizeof(ct));
  ct.on_connect = on_backend_connect;
  ct.on_recv_pkg = on_backend_recv_pkg;

  // 创建连接matchengine的rpc客户端
  matchengine = rpc_clt_create(&settings.matchengine, &ct);
  if (matchengine == NULL) {
    log_error("name: %s rpc_clt 创建失败", settings.matchengine.name);
    return -__LINE__;
  }
  if (rpc_clt_start(matchengine) < 0) { //启动
    log_error("name: %s rpc_clt 启动失败", settings.matchengine.name);
    return -__LINE__;
  }

  // 创建连接marketprice的rpc客户端
  marketprice = rpc_clt_create(&settings.marketprice, &ct);
  if (marketprice == NULL) {
    log_error("name: %s rpc_clt 创建失败", settings.marketprice.name);
    return -__LINE__;
  }
  if (rpc_clt_start(marketprice) < 0) { //启动
    log_error("name: %s rpc_clt 启动失败", settings.marketprice.name);
    return -__LINE__;
  }

  // 创建连接readhistory的连接客户端
  readhistory = rpc_clt_create(&settings.readhistory, &ct);
  if (readhistory == NULL) {
    log_error("name: %s rpc_clt 创建失败", settings.readhistory.name);
    return -__LINE__;
  }
  if (rpc_clt_start(readhistory) < 0) { //启动
    log_error("name: %s rpc_clt 启动失败", settings.readhistory.name);
    return -__LINE__;
  }

  /* 4. 定义http服务 */

  // 创建http服务器，只有一个事物即接收到请求时on_http_request
  svr = http_svr_create(&settings.svr, on_http_request);
  if (svr == NULL) {
    log_error("name: %s http_svr 创建失败", settings.svr.name);
    return -__LINE__;
  }
  log_info("name: %s http_svr 创建成功", settings.svr.name);

  /* 5.将method响应绑定到各后台服务的rpc客户端上*/
  //绑定http消息的rpc方法
  ERR_RET(init_methods_handler());

  /* 启动woker rpc_clt*/
  ERR_RET(init_woker_clt());

  return 0;
}
