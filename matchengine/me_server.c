/*
 * Description:
 *     History: yang@haipo.me, 2017/03/16, create
 */

#include "me_server.h"
#include "me_balance.h"
#include "me_config.h"
#include "me_history.h"
#include "me_market.h"
#include "me_message.h"
#include "me_operlog.h"
#include "me_trade.h"
#include "me_update.h"

static rpc_svr *svr;         //服务端
static dict_t *dict_cache;   // 缓存字典
static nw_timer cache_timer; // 缓存计时器

// 缓存数据
struct cache_val {
  double time;
  json_t *result;
};

/*1. 响应 */

//返回json
static int reply_json(nw_ses *ses, rpc_pkg *pkg, const json_t *json) {
  char *message_data;
  if (settings.debug) {
    message_data = json_dumps(json, JSON_INDENT(4));
  } else {
    message_data = json_dumps(json, 0);
  }
  if (message_data == NULL)
    return -__LINE__;
  log_trace("connection: %s send: %s", nw_sock_human_addr(&ses->peer_addr),
            message_data);

  rpc_pkg reply;
  memcpy(&reply, pkg, sizeof(reply));
  reply.pkg_type = RPC_PKG_TYPE_REPLY;
  reply.body = message_data;
  reply.body_size = strlen(message_data);
  rpc_send(ses, &reply);
  free(message_data);

  return 0;
}

//返回错误
static int reply_error(nw_ses *ses, rpc_pkg *pkg, int code,
                       const char *message) {
  json_t *error = json_object();
  json_object_set_new(error, "code", json_integer(code));
  json_object_set_new(error, "message", json_string(message));

  json_t *reply = json_object();
  json_object_set_new(reply, "error", error);
  json_object_set_new(reply, "result", json_null());
  json_object_set_new(reply, "id", json_integer(pkg->req_id));

  int ret = reply_json(ses, pkg, reply);
  json_decref(reply);

  return ret;
}

//返回错误：无效参数
static int reply_error_invalid_argument(nw_ses *ses, rpc_pkg *pkg) {
  return reply_error(ses, pkg, 1, "invalid argument");
}
//返回错误：内部错误
static int reply_error_internal_error(nw_ses *ses, rpc_pkg *pkg) {
  return reply_error(ses, pkg, 2, "internal error");
}
//返回错误：服务不可用
static int reply_error_service_unavailable(nw_ses *ses, rpc_pkg *pkg) {
  return reply_error(ses, pkg, 3, "service unavailable");
}

//返回结果
static int reply_result(nw_ses *ses, rpc_pkg *pkg, json_t *result) {
  json_t *reply = json_object();
  json_object_set_new(reply, "error", json_null());
  json_object_set(reply, "result", result);
  json_object_set_new(reply, "id", json_integer(pkg->req_id));

  int ret = reply_json(ses, pkg, reply);
  json_decref(reply);

  return ret;
}

//返回成功
static int reply_success(nw_ses *ses, rpc_pkg *pkg) {
  json_t *result = json_object();
  json_object_set_new(result, "status", json_string("success"));

  int ret = reply_result(ses, pkg, result);
  json_decref(result);
  return ret;
}

/*2. 缓存 */

//处理缓存
static bool process_cache(nw_ses *ses, rpc_pkg *pkg, sds *cache_key) {
  sds key = sdsempty();
  key = sdscatprintf(key, "%u", pkg->command);
  key = sdscatlen(key, pkg->body, pkg->body_size);
  dict_entry *entry = dict_find(dict_cache, key);
  if (entry == NULL) {
    *cache_key = key;
    return false;
  }

  struct cache_val *cache = entry->val;
  double now = current_timestamp();
  if ((now - cache->time) > settings.cache_timeout) {
    dict_delete(dict_cache, key);
    *cache_key = key;
    return false;
  }

  reply_result(ses, pkg, cache->result);
  sdsfree(key);
  return true;
}

//添加缓存
static int add_cache(sds cache_key, json_t *result) {
  struct cache_val cache;
  cache.time = current_timestamp();
  cache.result = result;
  json_incref(result);
  dict_replace(dict_cache, cache_key, &cache);

  return 0;
}

/*3. 事件 */

//事件：账户余额查询
static int on_cmd_balance_query(nw_ses *ses, rpc_pkg *pkg, json_t *params) {
  size_t request_size = json_array_size(params);
  if (request_size == 0)
    return reply_error_invalid_argument(ses, pkg);

  if (!json_is_integer(json_array_get(params, 0)))
    return reply_error_invalid_argument(ses, pkg);
  uint32_t user_id = json_integer_value(json_array_get(params, 0));
  if (user_id == 0)
    return reply_error_invalid_argument(ses, pkg);

  json_t *result = json_object();
  if (request_size == 1) {
    for (size_t i = 0; i < settings.asset_num; ++i) {
      const char *asset = settings.assets[i].name;
      json_t *unit = json_object();
      int prec_save = asset_prec(asset);
      int prec_show = asset_prec_show(asset);

      mpd_t *available = balance_get(user_id, BALANCE_TYPE_AVAILABLE, asset);
      if (available) {
        if (prec_save != prec_show) {
          mpd_t *show = mpd_qncopy(available);
          mpd_rescale(show, show, -prec_show, &mpd_ctx);
          json_object_set_new_mpd(unit, "available", show);
          mpd_del(show);
        } else {
          json_object_set_new_mpd(unit, "available", available);
        }
      } else {
        json_object_set_new(unit, "available", json_string("0"));
      }

      mpd_t *freeze = balance_get(user_id, BALANCE_TYPE_FREEZE, asset);
      if (freeze) {
        if (prec_save != prec_show) {
          mpd_t *show = mpd_qncopy(freeze);
          mpd_rescale(show, show, -prec_show, &mpd_ctx);
          json_object_set_new_mpd(unit, "freeze", show);
          mpd_del(show);
        } else {
          json_object_set_new_mpd(unit, "freeze", freeze);
        }
      } else {
        json_object_set_new(unit, "freeze", json_string("0"));
      }

      json_object_set_new(result, asset, unit);
    }
  } else {
    for (size_t i = 1; i < request_size; ++i) {
      const char *asset = json_string_value(json_array_get(params, i));
      if (!asset || !asset_exist(asset)) {
        json_decref(result);
        return reply_error_invalid_argument(ses, pkg);
      }
      json_t *unit = json_object();
      int prec_save = asset_prec(asset);
      int prec_show = asset_prec_show(asset);

      mpd_t *available = balance_get(user_id, BALANCE_TYPE_AVAILABLE, asset);
      if (available) {
        if (prec_save != prec_show) {
          mpd_t *show = mpd_qncopy(available);
          mpd_rescale(show, show, -prec_show, &mpd_ctx);
          json_object_set_new_mpd(unit, "available", show);
          mpd_del(show);
        } else {
          json_object_set_new_mpd(unit, "available", available);
        }
      } else {
        json_object_set_new(unit, "available", json_string("0"));
      }

      mpd_t *freeze = balance_get(user_id, BALANCE_TYPE_FREEZE, asset);
      if (freeze) {
        if (prec_save != prec_show) {
          mpd_t *show = mpd_qncopy(freeze);
          mpd_rescale(show, show, -prec_show, &mpd_ctx);
          json_object_set_new_mpd(unit, "freeze", show);
          mpd_del(show);
        } else {
          json_object_set_new_mpd(unit, "freeze", freeze);
        }
      } else {
        json_object_set_new(unit, "freeze", json_string("0"));
      }

      json_object_set_new(result, asset, unit);
    }
  }

  int ret = reply_result(ses, pkg, result);
  json_decref(result);
  return ret;
}

//事件：账户余额更新
static int on_cmd_balance_update(nw_ses *ses, rpc_pkg *pkg, json_t *params) {
  if (json_array_size(params) != 6)
    return reply_error_invalid_argument(ses, pkg);

  // user_id
  if (!json_is_integer(json_array_get(params, 0)))
    return reply_error_invalid_argument(ses, pkg);
  uint32_t user_id = json_integer_value(json_array_get(params, 0));

  // asset
  if (!json_is_string(json_array_get(params, 1)))
    return reply_error_invalid_argument(ses, pkg);
  const char *asset = json_string_value(json_array_get(params, 1));
  int prec = asset_prec_show(asset);
  if (prec < 0)
    return reply_error_invalid_argument(ses, pkg);

  // business
  if (!json_is_string(json_array_get(params, 2)))
    return reply_error_invalid_argument(ses, pkg);
  const char *business = json_string_value(json_array_get(params, 2));

  // business_id
  if (!json_is_integer(json_array_get(params, 3)))
    return reply_error_invalid_argument(ses, pkg);
  uint64_t business_id = json_integer_value(json_array_get(params, 3));

  // change
  if (!json_is_string(json_array_get(params, 4)))
    return reply_error_invalid_argument(ses, pkg);
  mpd_t *change = decimal(json_string_value(json_array_get(params, 4)), prec);
  if (change == NULL)
    return reply_error_invalid_argument(ses, pkg);

  // detail
  json_t *detail = json_array_get(params, 5);
  if (!json_is_object(detail)) {
    mpd_del(change);
    return reply_error_invalid_argument(ses, pkg);
  }

  int ret = update_user_balance(true, user_id, asset, business, business_id,
                                change, detail);
  mpd_del(change);
  if (ret == -1) {
    return reply_error(ses, pkg, 10, "repeat update");
  } else if (ret == -2) {
    return reply_error(ses, pkg, 11, "balance not enough");
  } else if (ret < 0) {
    return reply_error_internal_error(ses, pkg);
  }

  append_operlog("update_balance", params);
  return reply_success(ses, pkg);
}

//事件：资产列表
static int on_cmd_asset_list(nw_ses *ses, rpc_pkg *pkg, json_t *params) {
  json_t *result = json_array();
  for (int i = 0; i < settings.asset_num; ++i) {
    json_t *asset = json_object();
    json_object_set_new(asset, "name", json_string(settings.assets[i].name));
    json_object_set_new(asset, "prec",
                        json_integer(settings.assets[i].prec_show));
    json_array_append_new(result, asset);
  }

  int ret = reply_result(ses, pkg, result);
  json_decref(result);
  return ret;
}

//资产总计
static json_t *get_asset_summary(const char *name) {
  size_t available_count;
  size_t freeze_count;
  mpd_t *total = mpd_new(&mpd_ctx);
  mpd_t *available = mpd_new(&mpd_ctx);
  mpd_t *freeze = mpd_new(&mpd_ctx);
  balance_status(name, total, &available_count, available, &freeze_count,
                 freeze);

  json_t *obj = json_object();
  json_object_set_new(obj, "name", json_string(name));
  json_object_set_new_mpd(obj, "total_balance", total);
  json_object_set_new(obj, "available_count", json_integer(available_count));
  json_object_set_new_mpd(obj, "available_balance", available);
  json_object_set_new(obj, "freeze_count", json_integer(freeze_count));
  json_object_set_new_mpd(obj, "freeze_balance", freeze);

  mpd_del(total);
  mpd_del(available);
  mpd_del(freeze);

  return obj;
}

//事件：资产总计
static int on_cmd_asset_summary(nw_ses *ses, rpc_pkg *pkg, json_t *params) {
  json_t *result = json_array();
  if (json_array_size(params) == 0) {
    for (int i = 0; i < settings.asset_num; ++i) {
      json_array_append_new(result, get_asset_summary(settings.assets[i].name));
    }
  } else {
    for (int i = 0; i < json_array_size(params); ++i) {
      const char *asset = json_string_value(json_array_get(params, i));
      if (asset == NULL)
        goto invalid_argument;
      if (!asset_exist(asset))
        goto invalid_argument;
      json_array_append_new(result, get_asset_summary(asset));
    }
  }

  int ret = reply_result(ses, pkg, result);
  json_decref(result);
  return ret;

invalid_argument:
  json_decref(result);
  return reply_error_invalid_argument(ses, pkg);
}

//事件：提交限价单
/*
ses: 连接session
pkg: 数据包
params: 参数
*/
static int on_cmd_order_put_limit(nw_ses *ses, rpc_pkg *pkg, json_t *params) {

  // 检查参数个数必须是8个
  if (json_array_size(params) != 8)
    return reply_error_invalid_argument(ses, pkg);

  // 解析 user_id
  if (!json_is_integer(json_array_get(params, 0)))
    return reply_error_invalid_argument(ses, pkg);
  uint32_t user_id = json_integer_value(json_array_get(params, 0));

  // 解析 market交易对
  if (!json_is_string(json_array_get(params, 1)))
    return reply_error_invalid_argument(ses, pkg);
  const char *market_name = json_string_value(json_array_get(params, 1));
  market_t *market = get_market(market_name);
  if (market == NULL)
    return reply_error_invalid_argument(ses, pkg);

  // 解析side：买/卖
  if (!json_is_integer(json_array_get(params, 2)))
    return reply_error_invalid_argument(ses, pkg);
  uint32_t side = json_integer_value(json_array_get(params, 2));
  if (side != MARKET_ORDER_SIDE_ASK && side != MARKET_ORDER_SIDE_BID)
    return reply_error_invalid_argument(ses, pkg);

  mpd_t *amount = NULL;
  mpd_t *price = NULL;
  mpd_t *taker_fee = NULL;
  mpd_t *maker_fee = NULL;

  // 解析amount：数量
  if (!json_is_string(json_array_get(params, 3)))
    goto invalid_argument;
  amount =
      decimal(json_string_value(json_array_get(params, 3)), market->stock_prec);
  if (amount == NULL || mpd_cmp(amount, mpd_zero, &mpd_ctx) <= 0)
    goto invalid_argument;

  // 解析price：价格
  if (!json_is_string(json_array_get(params, 4)))
    goto invalid_argument;
  price =
      decimal(json_string_value(json_array_get(params, 4)), market->money_prec);
  if (price == NULL || mpd_cmp(price, mpd_zero, &mpd_ctx) <= 0)
    goto invalid_argument;

  // 解析成交费用：taker fee
  if (!json_is_string(json_array_get(params, 5)))
    goto invalid_argument;
  taker_fee =
      decimal(json_string_value(json_array_get(params, 5)), market->fee_prec);
  if (taker_fee == NULL || mpd_cmp(taker_fee, mpd_zero, &mpd_ctx) < 0 ||
      mpd_cmp(taker_fee, mpd_one, &mpd_ctx) >= 0)
    goto invalid_argument;

  // 解析出价费用：maker fee
  if (!json_is_string(json_array_get(params, 6)))
    goto invalid_argument;
  maker_fee =
      decimal(json_string_value(json_array_get(params, 6)), market->fee_prec);
  if (maker_fee == NULL || mpd_cmp(maker_fee, mpd_zero, &mpd_ctx) < 0 ||
      mpd_cmp(maker_fee, mpd_one, &mpd_ctx) >= 0)
    goto invalid_argument;

  // source
  if (!json_is_string(json_array_get(params, 7)))
    goto invalid_argument;
  const char *source = json_string_value(json_array_get(params, 7));
  if (strlen(source) >= SOURCE_MAX_LEN)
    goto invalid_argument;

  json_t *result = NULL;

  // 提交限价订单
  int ret = market_put_limit_order(true, &result, market, user_id, side, amount,
                                   price, taker_fee, maker_fee, source);

  mpd_del(amount);
  mpd_del(price);
  mpd_del(taker_fee);
  mpd_del(maker_fee);

  // 处理提交结果
  if (ret == -1) {
    return reply_error(ses, pkg, 10, "balance not enough");
  } else if (ret == -2) {
    return reply_error(ses, pkg, 11, "amount too small");
  } else if (ret < 0) {
    log_fatal("market_put_limit_order fail: %d", ret);
    return reply_error_internal_error(ses, pkg);
  }

  // 记录操作日志
  append_operlog("limit_order", params);

  //生成响应数据
  ret = reply_result(ses, pkg, result);
  json_decref(result);
  return ret;

invalid_argument:
  if (amount)
    mpd_del(amount);
  if (price)
    mpd_del(price);
  if (taker_fee)
    mpd_del(taker_fee);
  if (maker_fee)
    mpd_del(maker_fee);

  return reply_error_invalid_argument(ses, pkg);
}

//事件：提交市价单
static int on_cmd_order_put_market(nw_ses *ses, rpc_pkg *pkg, json_t *params) {
  if (json_array_size(params) != 6)
    return reply_error_invalid_argument(ses, pkg);

  // user_id
  if (!json_is_integer(json_array_get(params, 0)))
    return reply_error_invalid_argument(ses, pkg);
  uint32_t user_id = json_integer_value(json_array_get(params, 0));

  // market
  if (!json_is_string(json_array_get(params, 1)))
    return reply_error_invalid_argument(ses, pkg);
  const char *market_name = json_string_value(json_array_get(params, 1));
  market_t *market = get_market(market_name);
  if (market == NULL)
    return reply_error_invalid_argument(ses, pkg);

  // side
  if (!json_is_integer(json_array_get(params, 2)))
    return reply_error_invalid_argument(ses, pkg);
  uint32_t side = json_integer_value(json_array_get(params, 2));
  if (side != MARKET_ORDER_SIDE_ASK && side != MARKET_ORDER_SIDE_BID)
    return reply_error_invalid_argument(ses, pkg);

  mpd_t *amount = NULL;
  mpd_t *taker_fee = NULL;

  // amount
  if (!json_is_string(json_array_get(params, 3)))
    goto invalid_argument;
  amount =
      decimal(json_string_value(json_array_get(params, 3)), market->stock_prec);
  if (amount == NULL || mpd_cmp(amount, mpd_zero, &mpd_ctx) <= 0)
    goto invalid_argument;

  // taker fee
  if (!json_is_string(json_array_get(params, 4)))
    goto invalid_argument;
  taker_fee =
      decimal(json_string_value(json_array_get(params, 4)), market->fee_prec);
  if (taker_fee == NULL || mpd_cmp(taker_fee, mpd_zero, &mpd_ctx) < 0 ||
      mpd_cmp(taker_fee, mpd_one, &mpd_ctx) >= 0)
    goto invalid_argument;

  // source
  if (!json_is_string(json_array_get(params, 5)))
    goto invalid_argument;
  const char *source = json_string_value(json_array_get(params, 5));
  if (strlen(source) >= SOURCE_MAX_LEN)
    goto invalid_argument;

  json_t *result = NULL;

  // 提交市价订单
  int ret = market_put_market_order(true, &result, market, user_id, side,
                                    amount, taker_fee, source);

  mpd_del(amount);
  mpd_del(taker_fee);

  if (ret == -1) {
    return reply_error(ses, pkg, 10, "balance not enough");
  } else if (ret == -2) {
    return reply_error(ses, pkg, 11, "amount too small");
  } else if (ret == -3) {
    return reply_error(ses, pkg, 12, "no enough trader");
  } else if (ret < 0) {
    log_fatal("market_put_limit_order fail: %d", ret);
    return reply_error_internal_error(ses, pkg);
  }

  append_operlog("market_order", params);
  ret = reply_result(ses, pkg, result);
  json_decref(result);
  return ret;

invalid_argument:
  if (amount)
    mpd_del(amount);
  if (taker_fee)
    mpd_del(taker_fee);

  return reply_error_invalid_argument(ses, pkg);
}

//事件：订单查询
static int on_cmd_order_query(nw_ses *ses, rpc_pkg *pkg, json_t *params) {
  if (json_array_size(params) != 4)
    return reply_error_invalid_argument(ses, pkg);

  // user_id
  if (!json_is_integer(json_array_get(params, 0)))
    return reply_error_invalid_argument(ses, pkg);
  uint32_t user_id = json_integer_value(json_array_get(params, 0));

  // market
  if (!json_is_string(json_array_get(params, 1)))
    return reply_error_invalid_argument(ses, pkg);
  const char *market_name = json_string_value(json_array_get(params, 1));
  market_t *market = get_market(market_name);
  if (market == NULL)
    return reply_error_invalid_argument(ses, pkg);

  // offset
  if (!json_is_integer(json_array_get(params, 2)))
    return reply_error_invalid_argument(ses, pkg);
  size_t offset = json_integer_value(json_array_get(params, 2));

  // limit
  if (!json_is_integer(json_array_get(params, 3)))
    return reply_error_invalid_argument(ses, pkg);
  size_t limit = json_integer_value(json_array_get(params, 3));
  if (limit > ORDER_LIST_MAX_LEN)
    return reply_error_invalid_argument(ses, pkg);

  json_t *result = json_object();
  json_object_set_new(result, "limit", json_integer(limit));
  json_object_set_new(result, "offset", json_integer(offset));

  json_t *orders = json_array();
  skiplist_t *order_list = market_get_order_list(market, user_id);
  if (order_list == NULL) {
    json_object_set_new(result, "total", json_integer(0));
  } else {
    json_object_set_new(result, "total", json_integer(order_list->len));
    if (offset < order_list->len) {
      skiplist_iter *iter = skiplist_get_iterator(order_list);
      skiplist_node *node;
      for (size_t i = 0; i < offset; i++) {
        if (skiplist_next(iter) == NULL)
          break;
      }
      size_t index = 0;
      while ((node = skiplist_next(iter)) != NULL && index < limit) {
        index++;
        order_t *order = node->value;
        json_array_append_new(orders, get_order_info(order));
      }
      skiplist_release_iterator(iter);
    }
  }

  json_object_set_new(result, "records", orders);
  int ret = reply_result(ses, pkg, result);
  json_decref(result);
  return ret;
}

//事件：订单取消
static int on_cmd_order_cancel(nw_ses *ses, rpc_pkg *pkg, json_t *params) {
  if (json_array_size(params) != 3)
    return reply_error_invalid_argument(ses, pkg);

  // user_id
  if (!json_is_integer(json_array_get(params, 0)))
    return reply_error_invalid_argument(ses, pkg);
  uint32_t user_id = json_integer_value(json_array_get(params, 0));

  // market
  if (!json_is_string(json_array_get(params, 1)))
    return reply_error_invalid_argument(ses, pkg);
  const char *market_name = json_string_value(json_array_get(params, 1));
  market_t *market = get_market(market_name);
  if (market == NULL)
    return reply_error_invalid_argument(ses, pkg);

  // order_id
  if (!json_is_integer(json_array_get(params, 2)))
    return reply_error_invalid_argument(ses, pkg);
  uint64_t order_id = json_integer_value(json_array_get(params, 2));

  order_t *order = market_get_order(market, order_id);
  if (order == NULL) {
    return reply_error(ses, pkg, 10, "order not found");
  }
  if (order->user_id != user_id) {
    return reply_error(ses, pkg, 11, "user not match");
  }

  json_t *result = NULL;
  int ret = market_cancel_order(true, &result, market, order);
  if (ret < 0) {
    log_fatal("cancel order: %" PRIu64 " fail: %d", order->id, ret);
    return reply_error_internal_error(ses, pkg);
  }

  append_operlog("cancel_order", params);
  ret = reply_result(ses, pkg, result);
  json_decref(result);
  return ret;
}

//事件：订单深度
static int on_cmd_order_book(nw_ses *ses, rpc_pkg *pkg, json_t *params) {
  if (json_array_size(params) != 4)
    return reply_error_invalid_argument(ses, pkg);

  // market
  if (!json_is_string(json_array_get(params, 0)))
    return reply_error_invalid_argument(ses, pkg);
  const char *market_name = json_string_value(json_array_get(params, 0));
  market_t *market = get_market(market_name);
  if (market == NULL)
    return reply_error_invalid_argument(ses, pkg);

  // side
  if (!json_is_integer(json_array_get(params, 1)))
    return reply_error_invalid_argument(ses, pkg);
  uint32_t side = json_integer_value(json_array_get(params, 1));
  if (side != MARKET_ORDER_SIDE_ASK && side != MARKET_ORDER_SIDE_BID)
    return reply_error_invalid_argument(ses, pkg);

  // offset
  if (!json_is_integer(json_array_get(params, 2)))
    return reply_error_invalid_argument(ses, pkg);
  size_t offset = json_integer_value(json_array_get(params, 2));

  // limit
  if (!json_is_integer(json_array_get(params, 3)))
    return reply_error_invalid_argument(ses, pkg);
  size_t limit = json_integer_value(json_array_get(params, 3));
  if (limit > ORDER_BOOK_MAX_LEN)
    return reply_error_invalid_argument(ses, pkg);

  json_t *result = json_object();
  json_object_set_new(result, "offset", json_integer(offset));
  json_object_set_new(result, "limit", json_integer(limit));

  uint64_t total;
  skiplist_iter *iter;
  if (side == MARKET_ORDER_SIDE_ASK) {
    iter = skiplist_get_iterator(market->asks);
    total = market->asks->len;
    json_object_set_new(result, "total", json_integer(total));
  } else {
    iter = skiplist_get_iterator(market->bids);
    total = market->bids->len;
    json_object_set_new(result, "total", json_integer(total));
  }

  json_t *orders = json_array();
  if (offset < total) {
    for (size_t i = 0; i < offset; i++) {
      if (skiplist_next(iter) == NULL)
        break;
    }
    size_t index = 0;
    skiplist_node *node;
    while ((node = skiplist_next(iter)) != NULL && index < limit) {
      index++;
      order_t *order = node->value;
      json_array_append_new(orders, get_order_info(order));
    }
  }
  skiplist_release_iterator(iter);

  json_object_set_new(result, "orders", orders);
  int ret = reply_result(ses, pkg, result);
  json_decref(result);
  return ret;
}

//订单深度
static json_t *get_depth(market_t *market, size_t limit) {
  mpd_t *price = mpd_new(&mpd_ctx);
  mpd_t *amount = mpd_new(&mpd_ctx);

  json_t *asks = json_array();
  skiplist_iter *iter = skiplist_get_iterator(market->asks);
  skiplist_node *node = skiplist_next(iter);
  size_t index = 0;
  while (node && index < limit) {
    index++;
    order_t *order = node->value;
    mpd_copy(price, order->price, &mpd_ctx);
    mpd_copy(amount, order->left, &mpd_ctx);
    while ((node = skiplist_next(iter)) != NULL) {
      order = node->value;
      if (mpd_cmp(price, order->price, &mpd_ctx) == 0) {
        mpd_add(amount, amount, order->left, &mpd_ctx);
      } else {
        break;
      }
    }
    json_t *info = json_array();
    json_array_append_new_mpd(info, price);
    json_array_append_new_mpd(info, amount);
    json_array_append_new(asks, info);
  }
  skiplist_release_iterator(iter);

  json_t *bids = json_array();
  iter = skiplist_get_iterator(market->bids);
  node = skiplist_next(iter);
  index = 0;
  while (node && index < limit) {
    index++;
    order_t *order = node->value;
    mpd_copy(price, order->price, &mpd_ctx);
    mpd_copy(amount, order->left, &mpd_ctx);
    while ((node = skiplist_next(iter)) != NULL) {
      order = node->value;
      if (mpd_cmp(price, order->price, &mpd_ctx) == 0) {
        mpd_add(amount, amount, order->left, &mpd_ctx);
      } else {
        break;
      }
    }
    json_t *info = json_array();
    json_array_append_new_mpd(info, price);
    json_array_append_new_mpd(info, amount);
    json_array_append_new(bids, info);
  }
  skiplist_release_iterator(iter);

  mpd_del(price);
  mpd_del(amount);

  json_t *result = json_object();
  json_object_set_new(result, "asks", asks);
  json_object_set_new(result, "bids", bids);

  return result;
}

//事件：订单深度，合并的深度
static json_t *get_depth_merge(market_t *market, size_t limit,
                               mpd_t *interval) {
  mpd_t *q = mpd_new(&mpd_ctx);
  mpd_t *r = mpd_new(&mpd_ctx);
  mpd_t *price = mpd_new(&mpd_ctx);
  mpd_t *amount = mpd_new(&mpd_ctx);

  json_t *asks = json_array();
  skiplist_iter *iter = skiplist_get_iterator(market->asks);
  skiplist_node *node = skiplist_next(iter);
  size_t index = 0;
  while (node && index < limit) {
    index++;
    order_t *order = node->value;
    mpd_divmod(q, r, order->price, interval, &mpd_ctx);
    mpd_mul(price, q, interval, &mpd_ctx);
    if (mpd_cmp(r, mpd_zero, &mpd_ctx) != 0) {
      mpd_add(price, price, interval, &mpd_ctx);
    }
    mpd_copy(amount, order->left, &mpd_ctx);
    while ((node = skiplist_next(iter)) != NULL) {
      order = node->value;
      if (mpd_cmp(price, order->price, &mpd_ctx) >= 0) {
        mpd_add(amount, amount, order->left, &mpd_ctx);
      } else {
        break;
      }
    }
    json_t *info = json_array();
    json_array_append_new_mpd(info, price);
    json_array_append_new_mpd(info, amount);
    json_array_append_new(asks, info);
  }
  skiplist_release_iterator(iter);

  json_t *bids = json_array();
  iter = skiplist_get_iterator(market->bids);
  node = skiplist_next(iter);
  index = 0;
  while (node && index < limit) {
    index++;
    order_t *order = node->value;
    mpd_divmod(q, r, order->price, interval, &mpd_ctx);
    mpd_mul(price, q, interval, &mpd_ctx);
    mpd_copy(amount, order->left, &mpd_ctx);
    while ((node = skiplist_next(iter)) != NULL) {
      order = node->value;
      if (mpd_cmp(price, order->price, &mpd_ctx) <= 0) {
        mpd_add(amount, amount, order->left, &mpd_ctx);
      } else {
        break;
      }
    }

    json_t *info = json_array();
    json_array_append_new_mpd(info, price);
    json_array_append_new_mpd(info, amount);
    json_array_append_new(bids, info);
  }
  skiplist_release_iterator(iter);

  mpd_del(q);
  mpd_del(r);
  mpd_del(price);
  mpd_del(amount);

  json_t *result = json_object();
  json_object_set_new(result, "asks", asks);
  json_object_set_new(result, "bids", bids);

  return result;
}

//事件：订单深度
static int on_cmd_order_book_depth(nw_ses *ses, rpc_pkg *pkg, json_t *params) {
  if (json_array_size(params) != 3)
    return reply_error_invalid_argument(ses, pkg);

  // market
  if (!json_is_string(json_array_get(params, 0)))
    return reply_error_invalid_argument(ses, pkg);
  const char *market_name = json_string_value(json_array_get(params, 0));
  market_t *market = get_market(market_name);
  if (market == NULL)
    return reply_error_invalid_argument(ses, pkg);

  // limit
  if (!json_is_integer(json_array_get(params, 1)))
    return reply_error_invalid_argument(ses, pkg);
  size_t limit = json_integer_value(json_array_get(params, 1));
  if (limit > ORDER_BOOK_MAX_LEN)
    return reply_error_invalid_argument(ses, pkg);

  // interval
  if (!json_is_string(json_array_get(params, 2)))
    return reply_error_invalid_argument(ses, pkg);
  mpd_t *interval =
      decimal(json_string_value(json_array_get(params, 2)), market->money_prec);
  if (!interval)
    return reply_error_invalid_argument(ses, pkg);
  if (mpd_cmp(interval, mpd_zero, &mpd_ctx) < 0) {
    mpd_del(interval);
    return reply_error_invalid_argument(ses, pkg);
  }

  sds cache_key = NULL;
  if (process_cache(ses, pkg, &cache_key)) {
    mpd_del(interval);
    return 0;
  }

  json_t *result = NULL;
  if (mpd_cmp(interval, mpd_zero, &mpd_ctx) == 0) {
    result = get_depth(market, limit);
  } else {
    result = get_depth_merge(market, limit, interval);
  }
  mpd_del(interval);

  if (result == NULL) {
    sdsfree(cache_key);
    return reply_error_internal_error(ses, pkg);
  }

  add_cache(cache_key, result);
  sdsfree(cache_key);

  int ret = reply_result(ses, pkg, result);
  json_decref(result);
  return ret;
}

//事件：订单详情
static int on_cmd_order_detail(nw_ses *ses, rpc_pkg *pkg, json_t *params) {
  if (json_array_size(params) != 2)
    return reply_error_invalid_argument(ses, pkg);

  // market
  if (!json_is_string(json_array_get(params, 0)))
    return reply_error_invalid_argument(ses, pkg);
  const char *market_name = json_string_value(json_array_get(params, 0));
  market_t *market = get_market(market_name);
  if (market == NULL)
    return reply_error_invalid_argument(ses, pkg);

  // order_id
  if (!json_is_integer(json_array_get(params, 1)))
    return reply_error_invalid_argument(ses, pkg);
  uint64_t order_id = json_integer_value(json_array_get(params, 1));

  order_t *order = market_get_order(market, order_id);
  json_t *result = NULL;
  if (order == NULL) {
    result = json_null();
  } else {
    result = get_order_info(order);
  }

  int ret = reply_result(ses, pkg, result);
  json_decref(result);
  return ret;
}

//事件：交易对查询
static int on_cmd_market_list(nw_ses *ses, rpc_pkg *pkg, json_t *params) {
  json_t *result = json_array();
  for (int i = 0; i < settings.market_num; ++i) {
    json_t *market = json_object();
    json_object_set_new(market, "name", json_string(settings.markets[i].name));
    json_object_set_new(market, "stock",
                        json_string(settings.markets[i].stock));
    json_object_set_new(market, "money",
                        json_string(settings.markets[i].money));
    json_object_set_new(market, "fee_prec",
                        json_integer(settings.markets[i].fee_prec));
    json_object_set_new(market, "stock_prec",
                        json_integer(settings.markets[i].stock_prec));
    json_object_set_new(market, "money_prec",
                        json_integer(settings.markets[i].money_prec));
    json_object_set_new_mpd(market, "min_amount",
                            settings.markets[i].min_amount);
    json_array_append_new(result, market);
  }

  int ret = reply_result(ses, pkg, result);
  json_decref(result);
  return ret;
}

//交易对查询
static json_t *get_market_summary(const char *name) {
  size_t ask_count;
  size_t bid_count;
  mpd_t *ask_amount = mpd_new(&mpd_ctx);
  mpd_t *bid_amount = mpd_new(&mpd_ctx);
  market_t *market = get_market(name);
  market_get_status(market, &ask_count, ask_amount, &bid_count, bid_amount);

  json_t *obj = json_object();
  json_object_set_new(obj, "name", json_string(name));
  json_object_set_new(obj, "ask_count", json_integer(ask_count));
  json_object_set_new_mpd(obj, "ask_amount", ask_amount);
  json_object_set_new(obj, "bid_count", json_integer(bid_count));
  json_object_set_new_mpd(obj, "bid_amount", bid_amount);

  mpd_del(ask_amount);
  mpd_del(bid_amount);

  return obj;
}

//事件：交易对总计
static int on_cmd_market_summary(nw_ses *ses, rpc_pkg *pkg, json_t *params) {
  json_t *result = json_array();
  if (json_array_size(params) == 0) {
    for (int i = 0; i < settings.market_num; ++i) {
      json_array_append_new(result,
                            get_market_summary(settings.markets[i].name));
    }
  } else {
    for (int i = 0; i < json_array_size(params); ++i) {
      const char *market = json_string_value(json_array_get(params, i));
      if (market == NULL)
        goto invalid_argument;
      if (get_market(market) == NULL)
        goto invalid_argument;
      json_array_append_new(result, get_market_summary(market));
    }
  }

  int ret = reply_result(ses, pkg, result);
  json_decref(result);
  return ret;

invalid_argument:
  json_decref(result);
  return reply_error_invalid_argument(ses, pkg);
}

// 服务端接收到数据包时，根据数据包中的命令进行处理并响应
static void svr_on_recv_pkg(nw_ses *ses, rpc_pkg *pkg) {

  // 载入json参数
  json_t *params = json_loadb(pkg->body, pkg->body_size, 0, NULL);
  if (params == NULL || !json_is_array(params)) {
    goto decode_error;
  }
  // 参数的动态字符串
  sds params_str = sdsnewlen(pkg->body, pkg->body_size);

  // 结果变量return
  int ret;

  // 根据数据包中的命令进行处理并响应
  switch (pkg->command) {

    // 查询余额
  case CMD_BALANCE_QUERY:
    log_trace("from: %s cmd balance query, sequence: %u params: %s",
              nw_sock_human_addr(&ses->peer_addr), pkg->sequence, params_str);
    ret = on_cmd_balance_query(ses, pkg, params);
    if (ret < 0) {
      log_error("on_cmd_balance_query %s fail: %d", params_str, ret);
    }
    break;

    // 更新余额
  case CMD_BALANCE_UPDATE:
    if (is_operlog_block() || is_history_block() || is_message_block()) {
      log_fatal("service unavailable, operlog: %d, history: %d, message: %d",
                is_operlog_block(), is_history_block(), is_message_block());
      reply_error_service_unavailable(ses, pkg);
      goto cleanup;
    }
    log_trace("from: %s cmd balance update, sequence: %u params: %s",
              nw_sock_human_addr(&ses->peer_addr), pkg->sequence, params_str);
    ret = on_cmd_balance_update(ses, pkg, params);
    if (ret < 0) {
      log_error("on_cmd_balance_update %s fail: %d", params_str, ret);
    }
    break;

    // 列出资产表
  case CMD_ASSET_LIST:
    log_trace("from: %s cmd asset list, sequence: %u params: %s",
              nw_sock_human_addr(&ses->peer_addr), pkg->sequence, params_str);
    ret = on_cmd_asset_list(ses, pkg, params);
    if (ret < 0) {
      log_error("on_cmd_asset_list %s fail: %d", params_str, ret);
    }
    break;

    // 资产统计
  case CMD_ASSET_SUMMARY:
    log_trace("from: %s cmd asset summary, sequence: %u params: %s",
              nw_sock_human_addr(&ses->peer_addr), pkg->sequence, params_str);
    ret = on_cmd_asset_summary(ses, pkg, params);
    if (ret < 0) {
      log_error("on_cmd_asset_summary %s fail: %d", params_str, ret);
    }
    break;

    // 限价订单
  case CMD_ORDER_PUT_LIMIT:
    if (is_operlog_block() || is_history_block() || is_message_block()) {
      log_fatal("service unavailable, operlog: %d, history: %d, message: %d",
                is_operlog_block(), is_history_block(), is_message_block());
      reply_error_service_unavailable(ses, pkg);
      goto cleanup;
    }
    log_trace("from: %s cmd order put limit, sequence: %u params: %s",
              nw_sock_human_addr(&ses->peer_addr), pkg->sequence, params_str);
    ret = on_cmd_order_put_limit(ses, pkg, params);
    if (ret < 0) {
      log_error("on_cmd_order_put_limit %s fail: %d", params_str, ret);
    }
    break;

    // 市价订单
  case CMD_ORDER_PUT_MARKET:
    if (is_operlog_block() || is_history_block() || is_message_block()) {
      log_fatal("service unavailable, operlog: %d, history: %d, message: %d",
                is_operlog_block(), is_history_block(), is_message_block());
      reply_error_service_unavailable(ses, pkg);
      goto cleanup;
    }
    log_trace("from: %s cmd order put market, sequence: %u params: %s",
              nw_sock_human_addr(&ses->peer_addr), pkg->sequence, params_str);
    ret = on_cmd_order_put_market(ses, pkg, params);
    if (ret < 0) {
      log_error("on_cmd_order_put_market %s fail: %d", params_str, ret);
    }
    break;

    // 订单查询
  case CMD_ORDER_QUERY:
    log_trace("from: %s cmd order query, sequence: %u params: %s",
              nw_sock_human_addr(&ses->peer_addr), pkg->sequence, params_str);
    ret = on_cmd_order_query(ses, pkg, params);
    if (ret < 0) {
      log_error("on_cmd_order_query %s fail: %d", params_str, ret);
    }
    break;

    // 取消订单
  case CMD_ORDER_CANCEL:
    if (is_operlog_block() || is_history_block() || is_message_block()) {
      log_fatal("service unavailable, operlog: %d, history: %d, message: %d",
                is_operlog_block(), is_history_block(), is_message_block());
      reply_error_service_unavailable(ses, pkg);
      goto cleanup;
    }
    log_trace("from: %s cmd order cancel, sequence: %u params: %s",
              nw_sock_human_addr(&ses->peer_addr), pkg->sequence, params_str);
    ret = on_cmd_order_cancel(ses, pkg, params);
    if (ret < 0) {
      log_error("on_cmd_order_cancel %s fail: %d", params_str, ret);
    }
    break;

    // 订单表
  case CMD_ORDER_BOOK:
    log_trace("from: %s cmd order book, sequence: %u params: %s",
              nw_sock_human_addr(&ses->peer_addr), pkg->sequence, params_str);
    ret = on_cmd_order_book(ses, pkg, params);
    if (ret < 0) {
      log_error("on_cmd_order_book %s fail: %d", params_str, ret);
    }
    break;

    // 订单深度表
  case CMD_ORDER_BOOK_DEPTH:
    log_trace("from: %s cmd order book depth, sequence: %u params: %s",
              nw_sock_human_addr(&ses->peer_addr), pkg->sequence, params_str);
    ret = on_cmd_order_book_depth(ses, pkg, params);
    if (ret < 0) {
      log_error("on_cmd_order_book_depth %s fail: %d", params_str, ret);
    }
    break;

    // 订单详情
  case CMD_ORDER_DETAIL:
    log_trace("from: %s cmd order detail, sequence: %u params: %s",
              nw_sock_human_addr(&ses->peer_addr), pkg->sequence, params_str);
    ret = on_cmd_order_detail(ses, pkg, params);
    if (ret < 0) {
      log_error("on_cmd_order_detail %s fail: %d", params_str, ret);
    }
    break;

    // 列出交易对
  case CMD_MARKET_LIST:
    log_trace("from: %s cmd market list, sequence: %u params: %s",
              nw_sock_human_addr(&ses->peer_addr), pkg->sequence, params_str);
    ret = on_cmd_market_list(ses, pkg, params);
    if (ret < 0) {
      log_error("on_cmd_market_list %s fail: %d", params_str, ret);
    }
    break;

    // 交易对统计
  case CMD_MARKET_SUMMARY:
    log_trace("from: %s cmd market summary, sequence: %u params: %s",
              nw_sock_human_addr(&ses->peer_addr), pkg->sequence, params_str);
    ret = on_cmd_market_summary(ses, pkg, params);
    if (ret < 0) {
      log_error("on_cmd_market_summary%s fail: %d", params_str, ret);
    }
    break;

    // 无法识别的命令
  default:
    log_error("from: %s unknown command: %u",
              nw_sock_human_addr(&ses->peer_addr), pkg->command);
    break;
  }

// 释放数据
cleanup:
  sdsfree(params_str);
  json_decref(params);
  return;

// 解析错误处理
decode_error:
  if (params) {
    json_decref(params);
  }
  sds hex = hexdump(pkg->body, pkg->body_size);
  log_error("connection: %s, cmd: %u decode params fail, params data: \n%s",
            nw_sock_human_addr(&ses->peer_addr), pkg->command, hex);
  sdsfree(hex);
  rpc_svr_close_ses(svr, ses);

  return;
}

//服务器处理连接请求
static void svr_on_new_connection(nw_ses *ses) {
  //记录连接日志
  log_trace("new connection: %s", nw_sock_human_addr(&ses->peer_addr));
}

//服务器处理关闭请求
static void svr_on_connection_close(nw_ses *ses) {
  //记录关闭日志
  log_trace("connection: %s close", nw_sock_human_addr(&ses->peer_addr));
}

/* 缓存字典的相关函数 */

// hash函数
static uint32_t cache_dict_hash_function(const void *key) {
  return dict_generic_hash_function(key, sdslen((sds)key));
}
// 键比较
static int cache_dict_key_compare(const void *key1, const void *key2) {
  return sdscmp((sds)key1, (sds)key2);
}

//复制键的字符串
static void *cache_dict_key_dup(const void *key) {
  return sdsdup((const sds)key);
}

// 释放键的字符串
static void cache_dict_key_free(void *key) { sdsfree(key); }

//复制值的字符串
static void *cache_dict_val_dup(const void *val) {
  //分配内存
  struct cache_val *obj = malloc(sizeof(struct cache_val));
  //复制内容
  memcpy(obj, val, sizeof(struct cache_val));
  return obj;
}
// 释放值
static void cache_dict_val_free(void *val) {
  struct cache_val *obj = val;
  //释放对象的json内容
  json_decref(obj->result);
  free(val);
}

//事物：计时器
static void on_cache_timer(nw_timer *timer, void *privdata) {
  //清除缓存
  dict_clear(dict_cache);
}

//【程序入口】
int init_server(void) {

  // rpc服务类型，绑定处理方法
  rpc_svr_type type;
  memset(&type, 0, sizeof(type));

  //【VIP】
  type.on_recv_pkg = svr_on_recv_pkg; //绑定接收到数据包的事件
  type.on_new_connection = svr_on_new_connection;     //绑定连接事件
  type.on_connection_close = svr_on_connection_close; //绑定连接关闭事件

  //实例化rpc服务
  svr = rpc_svr_create(&settings.svr, &type);
  if (svr == NULL)
    return -__LINE__;

  //启动rpc服务
  if (rpc_svr_start(svr) < 0)
    return -__LINE__;

  //缓存字典
  dict_types dt;
  memset(&dt, 0, sizeof(dt));
  dt.hash_function = cache_dict_hash_function; //哈希方法
  dt.key_compare = cache_dict_key_compare;     //键比较方法
  dt.key_dup = cache_dict_key_dup;             //键重复
  dt.key_destructor = cache_dict_key_free;     //释放键
  dt.val_dup = cache_dict_val_dup;             //值重复
  dt.val_destructor = cache_dict_val_free;     //释放值

  dict_cache = dict_create(&dt, 64);
  if (dict_cache == NULL)
    return -__LINE__;

  nw_timer_set(&cache_timer, 60, true, on_cache_timer, NULL);
  nw_timer_start(&cache_timer); //启动计时器

  return 0;
}
