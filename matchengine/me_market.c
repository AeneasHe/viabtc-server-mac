/*
 * Description:
 *     History: yang@haipo.me, 2017/03/16, create
 *              cofe@outlook.com, 2019/05/17, comments
 */

/*
 *   交易对处理
 */

#include "me_market.h"
#include "me_balance.h"
#include "me_config.h"
#include "me_history.h"
#include "me_message.h"

uint64_t order_id_start; //订单id的初始值
uint64_t deals_id_start; //交易id的初始值

struct dict_user_key //用户字典的key
{
  uint32_t user_id;
};

struct dict_order_key //订单字典的key
{
  uint64_t order_id;
};

//创建用户字典的相关函数
static uint32_t dict_user_hash_function(const void *key) {
  const struct dict_user_key *obj = key;
  return obj->user_id;
}

static int dict_user_key_compare(const void *key1, const void *key2) {
  const struct dict_user_key *obj1 = key1;
  const struct dict_user_key *obj2 = key2;
  if (obj1->user_id == obj2->user_id) {
    return 0;
  }
  return 1;
}

static void *dict_user_key_dup(const void *key) {
  struct dict_user_key *obj = malloc(sizeof(struct dict_user_key));
  memcpy(obj, key, sizeof(struct dict_user_key));
  return obj;
}

static void dict_user_key_free(void *key) { free(key); }

static void dict_user_val_free(void *key) { skiplist_release(key); }

//创建订单字典的相关函数
static uint32_t dict_order_hash_function(const void *key) {
  return dict_generic_hash_function(key, sizeof(struct dict_order_key));
}

static int dict_order_key_compare(const void *key1, const void *key2) {
  const struct dict_order_key *obj1 = key1;
  const struct dict_order_key *obj2 = key2;
  if (obj1->order_id == obj2->order_id) {
    return 0;
  }
  return 1;
}

static void *dict_order_key_dup(const void *key) {
  struct dict_order_key *obj = malloc(sizeof(struct dict_order_key));
  memcpy(obj, key, sizeof(struct dict_order_key));
  return obj;
}

static void dict_order_key_free(void *key) { free(key); }

//订单价格比较
static int order_match_compare(const void *value1, const void *value2) {
  const order_t *order1 = value1;
  const order_t *order2 = value2;

  if (order1->id == order2->id) {
    return 0; //订单id相同，返回0
  }
  if (order1->type != order2->type) {
    return 1; //订单类型不相同，返回1
  }

  int cmp;
  if (order1->side == MARKET_ORDER_SIDE_ASK) //如果order1是买单
  {
    cmp = mpd_cmp(order1->price, order2->price, &mpd_ctx);
  } else {
    cmp = mpd_cmp(order2->price, order1->price, &mpd_ctx);
  }
  if (cmp != 0) {
    return cmp; //返回订单价格比较结果
  }

  return order1->id > order2->id ? 1 : -1; //返回订单id比较的大小
}

//订单id比较
static int order_id_compare(const void *value1, const void *value2) {
  const order_t *order1 = value1;
  const order_t *order2 = value2;
  if (order1->id == order2->id) {
    return 0;
  }

  return order1->id > order2->id ? -1 : 1;
}

//释放订单占用的内存
static void order_free(order_t *order) {
  mpd_del(order->price);
  mpd_del(order->amount);
  mpd_del(order->taker_fee);
  mpd_del(order->maker_fee);
  mpd_del(order->left);
  mpd_del(order->freeze);
  mpd_del(order->deal_stock);
  mpd_del(order->deal_money);
  mpd_del(order->deal_fee);
  free(order->market);
  free(order->source);
  free(order);
}

//获取订单信息
json_t *get_order_info(order_t *order) {
  json_t *info = json_object();
  json_object_set_new(info, "id", json_integer(order->id));
  json_object_set_new(info, "market", json_string(order->market));
  json_object_set_new(info, "source", json_string(order->source));
  json_object_set_new(info, "type", json_integer(order->type));
  json_object_set_new(info, "side", json_integer(order->side));
  json_object_set_new(info, "user", json_integer(order->user_id));
  json_object_set_new(info, "ctime", json_real(order->create_time));
  json_object_set_new(info, "mtime", json_real(order->update_time));

  json_object_set_new_mpd(info, "price", order->price);
  json_object_set_new_mpd(info, "amount", order->amount);
  json_object_set_new_mpd(info, "taker_fee", order->taker_fee);
  json_object_set_new_mpd(info, "maker_fee", order->maker_fee);
  json_object_set_new_mpd(info, "left", order->left);
  json_object_set_new_mpd(info, "deal_stock", order->deal_stock);
  json_object_set_new_mpd(info, "deal_money", order->deal_money);
  json_object_set_new_mpd(info, "deal_fee", order->deal_fee);

  return info;
}

//订单放入market
static int order_put(market_t *m, order_t *order) {
  if (order->type != MARKET_ORDER_TYPE_LIMIT) //检查是否是limit限价订单
    return -__LINE__;

  struct dict_order_key order_key = {.order_id = order->id};
  if (dict_add(m->orders, &order_key, order) ==
      NULL) //将订单id加入交易对的订单字典
    return -__LINE__;

  struct dict_user_key user_key = {
      .user_id = order->user_id}; //将用户id加入交易对的用户字典
  dict_entry *entry = dict_find(m->users, &user_key); //查找刚加入的用户

  if (entry) //如果找到刚加入的用户
  {
    skiplist_t *order_list = entry->val; //跳过的订单列表
    if (skiplist_insert(order_list, order) == NULL)
      return -__LINE__;
  } else //如果没有找到刚加入的用户
  {
    skiplist_type type;
    memset(&type, 0, sizeof(type));
    type.compare = order_id_compare;
    skiplist_t *order_list = skiplist_create(&type);
    if (order_list == NULL)
      return -__LINE__;
    if (skiplist_insert(order_list, order) == NULL)
      return -__LINE__;
    if (dict_add(m->users, &user_key, order_list) == NULL)
      return -__LINE__;
  }

  if (order->side == MARKET_ORDER_SIDE_ASK) //检查订单是否是市价买单
  {
    if (skiplist_insert(m->asks, order) == NULL)
      return -__LINE__;
    mpd_copy(order->freeze, order->left, &mpd_ctx);
    if (balance_freeze(order->user_id, m->stock, order->left) == NULL)
      return -__LINE__;
  } else //否则订单就是市价卖单
  {
    if (skiplist_insert(m->bids, order) == NULL)
      return -__LINE__;
    mpd_t *result = mpd_new(&mpd_ctx);
    mpd_mul(result, order->price, order->left, &mpd_ctx);
    mpd_copy(order->freeze, result, &mpd_ctx);
    if (balance_freeze(order->user_id, m->money, result) == NULL) {
      mpd_del(result);
      return -__LINE__;
    }
    mpd_del(result);
  }

  return 0;
}

//订单完成
static int order_finish(bool real, market_t *m, order_t *order) {
  if (order->side == MARKET_ORDER_SIDE_ASK) {
    skiplist_node *node = skiplist_find(m->asks, order);
    if (node) {
      skiplist_delete(m->asks, node);
    }
    if (mpd_cmp(order->freeze, mpd_zero, &mpd_ctx) > 0) {
      if (balance_unfreeze(order->user_id, m->stock, order->freeze) == NULL) {
        return -__LINE__;
      }
    }
  } else {
    skiplist_node *node = skiplist_find(m->bids, order);
    if (node) {
      skiplist_delete(m->bids, node);
    }
    if (mpd_cmp(order->freeze, mpd_zero, &mpd_ctx) > 0) {
      if (balance_unfreeze(order->user_id, m->money, order->freeze) == NULL) {
        return -__LINE__;
      }
    }
  }

  struct dict_order_key order_key = {.order_id = order->id};
  dict_delete(m->orders, &order_key);

  struct dict_user_key user_key = {.user_id = order->user_id};
  dict_entry *entry = dict_find(m->users, &user_key);
  if (entry) {
    skiplist_t *order_list = entry->val;
    skiplist_node *node = skiplist_find(order_list, order);
    if (node) {
      skiplist_delete(order_list, node);
    }
  }

  if (real) {
    if (mpd_cmp(order->deal_stock, mpd_zero, &mpd_ctx) > 0) {
      int ret = append_order_history(order);
      if (ret < 0) {
        log_fatal("append_order_history fail: %d, order: %" PRIu64 "", ret,
                  order->id);
      }
    }
  }

  order_free(order);
  return 0;
}

//创建新的交易对
market_t *market_create(struct market *conf) {

  // 配置参数检查
  if (!asset_exist(conf->stock) || !asset_exist(conf->money))
    return NULL;
  if (conf->stock_prec + conf->money_prec > asset_prec(conf->money))
    return NULL;
  if (conf->stock_prec + conf->fee_prec > asset_prec(conf->stock))
    return NULL;
  if (conf->money_prec + conf->fee_prec > asset_prec(conf->money))
    return NULL;

  // 分配内存
  market_t *m = malloc(sizeof(market_t));
  memset(m, 0, sizeof(market_t));

  // 绑定参数
  m->name = strdup(conf->name);
  m->stock = strdup(conf->stock);
  m->money = strdup(conf->money);
  m->stock_prec = conf->stock_prec;
  m->money_prec = conf->money_prec;
  m->fee_prec = conf->fee_prec;
  m->min_amount = mpd_qncopy(conf->min_amount);

  dict_types dt;

  // 用户字典
  memset(&dt, 0, sizeof(dt));
  dt.hash_function = dict_user_hash_function;
  dt.key_compare = dict_user_key_compare;
  dt.key_dup = dict_user_key_dup;
  dt.key_destructor = dict_user_key_free;
  dt.val_destructor = dict_user_val_free;

  m->users = dict_create(&dt, 1024);
  if (m->users == NULL)
    return NULL;

  //订单字典
  memset(&dt, 0, sizeof(dt));
  dt.hash_function = dict_order_hash_function;
  dt.key_compare = dict_order_key_compare;
  dt.key_dup = dict_order_key_dup;
  dt.key_destructor = dict_order_key_free;

  m->orders = dict_create(&dt, 1024);
  if (m->orders == NULL)
    return NULL;

  // asks与bids列表
  skiplist_type lt;
  memset(&lt, 0, sizeof(lt));
  lt.compare = order_match_compare;

  m->asks = skiplist_create(&lt);
  m->bids = skiplist_create(&lt);
  if (m->asks == NULL || m->bids == NULL)
    return NULL;

  return m;
}

//保存历史记录
static int append_balance_trade_add(order_t *order, const char *asset,
                                    mpd_t *change, mpd_t *price,
                                    mpd_t *amount) {
  json_t *detail = json_object();
  json_object_set_new(detail, "m", json_string(order->market));
  json_object_set_new(detail, "i", json_integer(order->id));
  json_object_set_new_mpd(detail, "p", price);
  json_object_set_new_mpd(detail, "a", amount);
  char *detail_str = json_dumps(detail, JSON_SORT_KEYS);
  int ret = append_user_balance_history(order->update_time, order->user_id,
                                        asset, "trade", change, detail_str);
  free(detail_str);
  json_decref(detail);
  return ret;
}

//保存历史记录
static int append_balance_trade_sub(order_t *order, const char *asset,
                                    mpd_t *change, mpd_t *price,
                                    mpd_t *amount) {
  json_t *detail = json_object();
  json_object_set_new(detail, "m", json_string(order->market));
  json_object_set_new(detail, "i", json_integer(order->id));
  json_object_set_new_mpd(detail, "p", price);
  json_object_set_new_mpd(detail, "a", amount);
  char *detail_str = json_dumps(detail, JSON_SORT_KEYS);
  mpd_t *real_change = mpd_new(&mpd_ctx);
  mpd_copy_negate(real_change, change, &mpd_ctx);
  int ret =
      append_user_balance_history(order->update_time, order->user_id, asset,
                                  "trade", real_change, detail_str);
  mpd_del(real_change);
  free(detail_str);
  json_decref(detail);
  return ret;
}

//保存历史记录
static int append_balance_trade_fee(order_t *order, const char *asset,
                                    mpd_t *change, mpd_t *price, mpd_t *amount,
                                    mpd_t *fee_rate) {
  json_t *detail = json_object();
  json_object_set_new(detail, "m", json_string(order->market));
  json_object_set_new(detail, "i", json_integer(order->id));
  json_object_set_new_mpd(detail, "p", price);
  json_object_set_new_mpd(detail, "a", amount);
  json_object_set_new_mpd(detail, "f", fee_rate);
  char *detail_str = json_dumps(detail, JSON_SORT_KEYS);
  mpd_t *real_change = mpd_new(&mpd_ctx);
  mpd_copy_negate(real_change, change, &mpd_ctx);
  int ret =
      append_user_balance_history(order->update_time, order->user_id, asset,
                                  "trade", real_change, detail_str);
  mpd_del(real_change);
  free(detail_str);
  json_decref(detail);
  return ret;
}

//执行限价买单
// real : realtime 是实时提交的数据，还是载入历史数据
static int execute_limit_ask_order(bool real, market_t *m, order_t *taker) {

  mpd_t *price = mpd_new(&mpd_ctx);
  mpd_t *amount = mpd_new(&mpd_ctx);
  mpd_t *deal = mpd_new(&mpd_ctx);
  mpd_t *ask_fee = mpd_new(&mpd_ctx);
  mpd_t *bid_fee = mpd_new(&mpd_ctx);
  mpd_t *result = mpd_new(&mpd_ctx);

  //询价出价列表
  skiplist_node *node;
  skiplist_iter *iter = skiplist_get_iterator(m->bids);

  while ((node = skiplist_next(iter)) != NULL) {
    if (mpd_cmp(taker->left, mpd_zero, &mpd_ctx) == 0) {
      break;
    }
    // 从深度提供表取出一个maker
    order_t *maker = node->value;

    if (mpd_cmp(taker->price, maker->price, &mpd_ctx) > 0) {
      //如果taker价格高于maker,跳过该maker
      break;
    }
    // 价格：采用卖方价格
    mpd_copy(price, maker->price, &mpd_ctx);

    // 交易量：采用双方较小的交易余量
    if (mpd_cmp(taker->left, maker->left, &mpd_ctx) < 0) {
      mpd_copy(amount, taker->left, &mpd_ctx);
    } else {
      mpd_copy(amount, maker->left, &mpd_ctx);
    }

    // 成交量
    mpd_mul(deal, price, amount, &mpd_ctx);
    // ask方的费用
    mpd_mul(ask_fee, deal, taker->taker_fee, &mpd_ctx);
    // maker方的费用
    mpd_mul(bid_fee, amount, maker->maker_fee, &mpd_ctx);

    // taker更新时间
    taker->update_time = maker->update_time = current_timestamp();

    // 成交id
    uint64_t deal_id = ++deals_id_start;

    if (real) {
      //写入成交日志
      append_order_deal_history(taker->update_time, deal_id, taker,
                                MARKET_ROLE_TAKER, maker, MARKET_ROLE_MAKER,
                                price, amount, deal, ask_fee, bid_fee);
      // 推送成交消息
      push_deal_message(taker->update_time, m->name, taker, maker, price,
                        amount, ask_fee, bid_fee, MARKET_ORDER_SIDE_ASK,
                        deal_id, m->stock, m->money);
    }

    /* 处理剩余事项 */

    /* taker  */
    //更新taker余量
    mpd_sub(taker->left, taker->left, amount, &mpd_ctx);
    // 更新taker的标的成交量
    mpd_add(taker->deal_stock, taker->deal_stock, amount, &mpd_ctx);
    // 更新taker的资金成交量
    mpd_add(taker->deal_money, taker->deal_money, deal, &mpd_ctx);
    // 更新taker的成交费用
    mpd_add(taker->deal_fee, taker->deal_fee, ask_fee, &mpd_ctx);

    //更新taker的标的余额
    balance_sub(taker->user_id, BALANCE_TYPE_AVAILABLE, m->stock, amount);
    if (real) {
      append_balance_trade_sub(taker, m->stock, amount, price, amount);
    }
    //更新taker的资金余额
    balance_add(taker->user_id, BALANCE_TYPE_AVAILABLE, m->money, deal);
    if (real) {
      append_balance_trade_add(taker, m->money, deal, price, amount);
    }
    // 扣除成交费用
    if (mpd_cmp(ask_fee, mpd_zero, &mpd_ctx) > 0) {
      balance_sub(taker->user_id, BALANCE_TYPE_AVAILABLE, m->money, ask_fee);
      if (real) {
        append_balance_trade_fee(taker, m->money, ask_fee, price, amount,
                                 taker->taker_fee);
      }
    }

    /* maker */
    // 余量
    mpd_sub(maker->left, maker->left, amount, &mpd_ctx);
    // 冻结量
    mpd_sub(maker->freeze, maker->freeze, deal, &mpd_ctx);
    // 成交标的量
    mpd_add(maker->deal_stock, maker->deal_stock, amount, &mpd_ctx);
    // 成交资金量
    mpd_add(maker->deal_money, maker->deal_money, deal, &mpd_ctx);
    // 成交费用
    mpd_add(maker->deal_fee, maker->deal_fee, bid_fee, &mpd_ctx);

    // 资金余额
    balance_sub(maker->user_id, BALANCE_TYPE_FREEZE, m->money, deal);
    if (real) {
      append_balance_trade_sub(maker, m->money, deal, price, amount);
    }

    // 标的余额
    balance_add(maker->user_id, BALANCE_TYPE_AVAILABLE, m->stock, amount);
    if (real) {
      append_balance_trade_add(maker, m->stock, amount, price, amount);
    }
    // 扣除费用
    if (mpd_cmp(bid_fee, mpd_zero, &mpd_ctx) > 0) {
      balance_sub(maker->user_id, BALANCE_TYPE_AVAILABLE, m->stock, bid_fee);
      if (real) {
        append_balance_trade_fee(maker, m->stock, bid_fee, price, amount,
                                 maker->maker_fee);
      }
    }
    // 推送消息
    if (mpd_cmp(maker->left, mpd_zero, &mpd_ctx) == 0) {
      if (real) {
        push_order_message(ORDER_EVENT_FINISH, maker, m);
      }
      order_finish(real, m, maker);
    } else {
      if (real) {
        push_order_message(ORDER_EVENT_UPDATE, maker, m);
      }
    }
  }

  // 释放skiplist遍历器
  skiplist_release_iterator(iter);

  // 释放参数变量
  mpd_del(amount);
  mpd_del(price);
  mpd_del(deal);
  mpd_del(ask_fee);
  mpd_del(bid_fee);
  mpd_del(result);

  return 0;
}

//执行限价卖单
static int execute_limit_bid_order(bool real, market_t *m, order_t *taker) {
  mpd_t *price = mpd_new(&mpd_ctx);
  mpd_t *amount = mpd_new(&mpd_ctx);
  mpd_t *deal = mpd_new(&mpd_ctx);
  mpd_t *ask_fee = mpd_new(&mpd_ctx);
  mpd_t *bid_fee = mpd_new(&mpd_ctx);
  mpd_t *result = mpd_new(&mpd_ctx);

  skiplist_node *node;
  skiplist_iter *iter = skiplist_get_iterator(m->asks);
  while ((node = skiplist_next(iter)) != NULL) {
    if (mpd_cmp(taker->left, mpd_zero, &mpd_ctx) == 0) {
      break;
    }

    order_t *maker = node->value;
    if (mpd_cmp(taker->price, maker->price, &mpd_ctx) < 0) {
      break;
    }

    mpd_copy(price, maker->price, &mpd_ctx);
    if (mpd_cmp(taker->left, maker->left, &mpd_ctx) < 0) {
      mpd_copy(amount, taker->left, &mpd_ctx);
    } else {
      mpd_copy(amount, maker->left, &mpd_ctx);
    }

    mpd_mul(deal, price, amount, &mpd_ctx);
    mpd_mul(ask_fee, deal, maker->maker_fee, &mpd_ctx);
    mpd_mul(bid_fee, amount, taker->taker_fee, &mpd_ctx);

    taker->update_time = maker->update_time = current_timestamp();
    uint64_t deal_id = ++deals_id_start;
    if (real) {
      append_order_deal_history(taker->update_time, deal_id, maker,
                                MARKET_ROLE_MAKER, taker, MARKET_ROLE_TAKER,
                                price, amount, deal, ask_fee, bid_fee);
      push_deal_message(taker->update_time, m->name, maker, taker, price,
                        amount, ask_fee, bid_fee, MARKET_ORDER_SIDE_BID,
                        deal_id, m->stock, m->money);
    }

    mpd_sub(taker->left, taker->left, amount, &mpd_ctx);
    mpd_add(taker->deal_stock, taker->deal_stock, amount, &mpd_ctx);
    mpd_add(taker->deal_money, taker->deal_money, deal, &mpd_ctx);
    mpd_add(taker->deal_fee, taker->deal_fee, bid_fee, &mpd_ctx);

    balance_sub(taker->user_id, BALANCE_TYPE_AVAILABLE, m->money, deal);
    if (real) {
      append_balance_trade_sub(taker, m->money, deal, price, amount);
    }
    balance_add(taker->user_id, BALANCE_TYPE_AVAILABLE, m->stock, amount);
    if (real) {
      append_balance_trade_add(taker, m->stock, amount, price, amount);
    }
    if (mpd_cmp(bid_fee, mpd_zero, &mpd_ctx) > 0) {
      balance_sub(taker->user_id, BALANCE_TYPE_AVAILABLE, m->stock, bid_fee);
      if (real) {
        append_balance_trade_fee(taker, m->stock, bid_fee, price, amount,
                                 taker->taker_fee);
      }
    }

    mpd_sub(maker->left, maker->left, amount, &mpd_ctx);
    mpd_sub(maker->freeze, maker->freeze, amount, &mpd_ctx);
    mpd_add(maker->deal_stock, maker->deal_stock, amount, &mpd_ctx);
    mpd_add(maker->deal_money, maker->deal_money, deal, &mpd_ctx);
    mpd_add(maker->deal_fee, maker->deal_fee, ask_fee, &mpd_ctx);

    balance_sub(maker->user_id, BALANCE_TYPE_FREEZE, m->stock, amount);
    if (real) {
      append_balance_trade_sub(maker, m->stock, amount, price, amount);
    }
    balance_add(maker->user_id, BALANCE_TYPE_AVAILABLE, m->money, deal);
    if (real) {
      append_balance_trade_add(maker, m->money, deal, price, amount);
    }
    if (mpd_cmp(ask_fee, mpd_zero, &mpd_ctx) > 0) {
      balance_sub(maker->user_id, BALANCE_TYPE_AVAILABLE, m->money, ask_fee);
      if (real) {
        append_balance_trade_fee(maker, m->money, ask_fee, price, amount,
                                 maker->maker_fee);
      }
    }

    if (mpd_cmp(maker->left, mpd_zero, &mpd_ctx) == 0) {
      if (real) {
        push_order_message(ORDER_EVENT_FINISH, maker, m);
      }
      order_finish(real, m, maker);
    } else {
      if (real) {
        push_order_message(ORDER_EVENT_UPDATE, maker, m);
      }
    }
  }
  skiplist_release_iterator(iter);

  mpd_del(amount);
  mpd_del(price);
  mpd_del(deal);
  mpd_del(ask_fee);
  mpd_del(bid_fee);
  mpd_del(result);

  return 0;
}

//将限价订单加入交易对撮合池
int market_put_limit_order(bool real, json_t **result, market_t *m,
                           uint32_t user_id, uint32_t side, mpd_t *amount,
                           mpd_t *price, mpd_t *taker_fee, mpd_t *maker_fee,
                           const char *source) {

  /* 检查余额 */

  // 卖出
  if (side == MARKET_ORDER_SIDE_ASK) {
    //查询余额
    mpd_t *balance = balance_get(user_id, BALANCE_TYPE_AVAILABLE, m->stock);
    if (!balance || mpd_cmp(balance, amount, &mpd_ctx) < 0) {
      return -1;
    }
  }
  // 买入
  else {
    // 查询资金余额
    mpd_t *balance = balance_get(user_id, BALANCE_TYPE_AVAILABLE, m->money);
    // 计算买入需要的资金
    mpd_t *require = mpd_new(&mpd_ctx);
    mpd_mul(require, amount, price, &mpd_ctx);

    // 如果买入需要的资金足够
    if (!balance || mpd_cmp(balance, require, &mpd_ctx) < 0) {
      mpd_del(require);
      return -1;
    }
    mpd_del(require);
  }

  // 检查最小交易量
  if (mpd_cmp(amount, m->min_amount, &mpd_ctx) < 0) {
    return -2;
  }

  // 创建订单对象
  order_t *order = malloc(sizeof(order_t));
  if (order == NULL) {
    return -__LINE__;
  }

  // 绑定订单参数
  order->id = ++order_id_start;             // 订单id
  order->type = MARKET_ORDER_TYPE_LIMIT;    // 订单类型：限价单
  order->side = side;                       // 订单买入卖出
  order->create_time = current_timestamp(); // 时间戳
  order->update_time = order->create_time;  // 更新时间戳
  order->market = strdup(m->name);          //交易对名称
  order->source = strdup(source);           // source
  order->user_id = user_id;                 //用户id

  // 订单高精度部分初始化
  order->price = mpd_new(&mpd_ctx);      //价格
  order->amount = mpd_new(&mpd_ctx);     //数量
  order->taker_fee = mpd_new(&mpd_ctx);  // 拿走深度费用
  order->maker_fee = mpd_new(&mpd_ctx);  //创建深度费用
  order->left = mpd_new(&mpd_ctx);       //余额
  order->freeze = mpd_new(&mpd_ctx);     //冻结
  order->deal_stock = mpd_new(&mpd_ctx); //交易的标的
  order->deal_money = mpd_new(&mpd_ctx); //交易的资金
  order->deal_fee = mpd_new(&mpd_ctx);   //交易的费用

  // 订单高精度部分赋值
  mpd_copy(order->price, price, &mpd_ctx);
  mpd_copy(order->amount, amount, &mpd_ctx);
  mpd_copy(order->taker_fee, taker_fee, &mpd_ctx);
  mpd_copy(order->maker_fee, maker_fee, &mpd_ctx);
  mpd_copy(order->left, amount, &mpd_ctx);
  mpd_copy(order->freeze, mpd_zero, &mpd_ctx);
  mpd_copy(order->deal_stock, mpd_zero, &mpd_ctx);
  mpd_copy(order->deal_money, mpd_zero, &mpd_ctx);
  mpd_copy(order->deal_fee, mpd_zero, &mpd_ctx);

  // 处理结果
  int ret;
  if (side == MARKET_ORDER_SIDE_ASK) {
    //执行询价单
    ret = execute_limit_ask_order(real, m, order);
  } else {
    //执行出价单
    ret = execute_limit_bid_order(real, m, order);
  }

  if (ret < 0) {
    //执行失败
    log_error("execute order: %" PRIu64 " fail: %d", order->id, ret);
    order_free(order);
    return -__LINE__;
  }

  // 如果订单余量为0
  if (mpd_cmp(order->left, mpd_zero, &mpd_ctx) == 0) {
    if (real) {
      // 创建订单日志
      ret = append_order_history(order);
      if (ret < 0) {
        log_fatal("append_order_history fail: %d, order: %" PRIu64 "", ret,
                  order->id);
      }
      // 推送订单消息
      push_order_message(ORDER_EVENT_FINISH, order, m);
      // 查询订单消息
      *result = get_order_info(order);
    }
    order_free(order);
  } else {
    if (real) {
      push_order_message(ORDER_EVENT_PUT, order, m);
      *result = get_order_info(order);
    }
    ret = order_put(m, order);
    if (ret < 0) {
      log_fatal("order_put fail: %d, order: %" PRIu64 "", ret, order->id);
    }
  }

  return 0;
}

//执行市价买单
static int execute_market_ask_order(bool real, market_t *m, order_t *taker) {
  mpd_t *price = mpd_new(&mpd_ctx);
  mpd_t *amount = mpd_new(&mpd_ctx);
  mpd_t *deal = mpd_new(&mpd_ctx);
  mpd_t *ask_fee = mpd_new(&mpd_ctx);
  mpd_t *bid_fee = mpd_new(&mpd_ctx);
  mpd_t *result = mpd_new(&mpd_ctx);

  // 变量列表
  skiplist_node *node;
  skiplist_iter *iter = skiplist_get_iterator(m->bids);
  while ((node = skiplist_next(iter)) != NULL) {

    // 如果taker的余量为0，跳过taker
    if (mpd_cmp(taker->left, mpd_zero, &mpd_ctx) == 0) {
      break;
    }

    order_t *maker = node->value;

    // 成交价格：maker的价格
    mpd_copy(price, maker->price, &mpd_ctx);

    // 成交量：双方的较小量
    if (mpd_cmp(taker->left, maker->left, &mpd_ctx) < 0) {
      mpd_copy(amount, taker->left, &mpd_ctx);
    } else {
      mpd_copy(amount, maker->left, &mpd_ctx);
    }

    // 成交量
    mpd_mul(deal, price, amount, &mpd_ctx);
    // ask方费用
    mpd_mul(ask_fee, deal, taker->taker_fee, &mpd_ctx);
    // bid方费用
    mpd_mul(bid_fee, amount, maker->maker_fee, &mpd_ctx);

    taker->update_time = maker->update_time = current_timestamp();

    // 成交id
    uint64_t deal_id = ++deals_id_start;

    if (real) {
      //记录成交日志
      append_order_deal_history(taker->update_time, deal_id, taker,
                                MARKET_ROLE_TAKER, maker, MARKET_ROLE_MAKER,
                                price, amount, deal, ask_fee, bid_fee);
      //推送成交消息
      push_deal_message(taker->update_time, m->name, taker, maker, price,
                        amount, ask_fee, bid_fee, MARKET_ORDER_SIDE_ASK,
                        deal_id, m->stock, m->money);
    }

    /* taker方处理事项 */
    mpd_sub(taker->left, taker->left, amount, &mpd_ctx);
    mpd_add(taker->deal_stock, taker->deal_stock, amount, &mpd_ctx);
    mpd_add(taker->deal_money, taker->deal_money, deal, &mpd_ctx);
    mpd_add(taker->deal_fee, taker->deal_fee, ask_fee, &mpd_ctx);

    balance_sub(taker->user_id, BALANCE_TYPE_AVAILABLE, m->stock, amount);
    if (real) {
      append_balance_trade_sub(taker, m->stock, amount, price, amount);
    }
    balance_add(taker->user_id, BALANCE_TYPE_AVAILABLE, m->money, deal);
    if (real) {
      append_balance_trade_add(taker, m->money, deal, price, amount);
    }
    if (mpd_cmp(ask_fee, mpd_zero, &mpd_ctx) > 0) {
      balance_sub(taker->user_id, BALANCE_TYPE_AVAILABLE, m->money, ask_fee);
      if (real) {
        append_balance_trade_fee(taker, m->money, ask_fee, price, amount,
                                 taker->taker_fee);
      }
    }

    /* maker方处理事项 */
    mpd_sub(maker->left, maker->left, amount, &mpd_ctx);
    mpd_sub(maker->freeze, maker->freeze, deal, &mpd_ctx);
    mpd_add(maker->deal_stock, maker->deal_stock, amount, &mpd_ctx);
    mpd_add(maker->deal_money, maker->deal_money, deal, &mpd_ctx);
    mpd_add(maker->deal_fee, maker->deal_fee, bid_fee, &mpd_ctx);

    balance_sub(maker->user_id, BALANCE_TYPE_FREEZE, m->money, deal);
    if (real) {
      append_balance_trade_sub(maker, m->money, deal, price, amount);
    }
    balance_add(maker->user_id, BALANCE_TYPE_AVAILABLE, m->stock, amount);
    if (real) {
      append_balance_trade_add(maker, m->stock, amount, price, amount);
    }
    if (mpd_cmp(bid_fee, mpd_zero, &mpd_ctx) > 0) {
      balance_sub(maker->user_id, BALANCE_TYPE_AVAILABLE, m->stock, bid_fee);
      if (real) {
        append_balance_trade_fee(maker, m->stock, bid_fee, price, amount,
                                 maker->maker_fee);
      }
    }
    // 推送消息
    if (mpd_cmp(maker->left, mpd_zero, &mpd_ctx) == 0) {
      if (real) {
        push_order_message(ORDER_EVENT_FINISH, maker, m);
      }
      order_finish(real, m, maker);
    } else {
      if (real) {
        push_order_message(ORDER_EVENT_UPDATE, maker, m);
      }
    }
  }
  skiplist_release_iterator(iter);

  mpd_del(amount);
  mpd_del(price);
  mpd_del(deal);
  mpd_del(ask_fee);
  mpd_del(bid_fee);
  mpd_del(result);

  return 0;
}

//执行市价卖单
static int execute_market_bid_order(bool real, market_t *m, order_t *taker) {
  mpd_t *price = mpd_new(&mpd_ctx);
  mpd_t *amount = mpd_new(&mpd_ctx);
  mpd_t *deal = mpd_new(&mpd_ctx);
  mpd_t *ask_fee = mpd_new(&mpd_ctx);
  mpd_t *bid_fee = mpd_new(&mpd_ctx);
  mpd_t *result = mpd_new(&mpd_ctx);

  skiplist_node *node;
  skiplist_iter *iter = skiplist_get_iterator(m->asks);
  while ((node = skiplist_next(iter)) != NULL) {
    if (mpd_cmp(taker->left, mpd_zero, &mpd_ctx) == 0) {
      break;
    }

    order_t *maker = node->value;
    mpd_copy(price, maker->price, &mpd_ctx);

    mpd_div(amount, taker->left, price, &mpd_ctx);
    mpd_rescale(amount, amount, -m->stock_prec, &mpd_ctx);
    while (true) {
      mpd_mul(result, amount, price, &mpd_ctx);
      if (mpd_cmp(result, taker->left, &mpd_ctx) > 0) {
        mpd_set_i32(result, -m->stock_prec, &mpd_ctx);
        mpd_pow(result, mpd_ten, result, &mpd_ctx);
        mpd_sub(amount, amount, result, &mpd_ctx);
      } else {
        break;
      }
    }

    if (mpd_cmp(amount, maker->left, &mpd_ctx) > 0) {
      mpd_copy(amount, maker->left, &mpd_ctx);
    }
    if (mpd_cmp(amount, mpd_zero, &mpd_ctx) == 0) {
      break;
    }

    mpd_mul(deal, price, amount, &mpd_ctx);
    mpd_mul(ask_fee, deal, maker->maker_fee, &mpd_ctx);
    mpd_mul(bid_fee, amount, taker->taker_fee, &mpd_ctx);

    taker->update_time = maker->update_time = current_timestamp();
    uint64_t deal_id = ++deals_id_start;
    if (real) {
      append_order_deal_history(taker->update_time, deal_id, maker,
                                MARKET_ROLE_MAKER, taker, MARKET_ROLE_TAKER,
                                price, amount, deal, ask_fee, bid_fee);
      push_deal_message(taker->update_time, m->name, maker, taker, price,
                        amount, ask_fee, bid_fee, MARKET_ORDER_SIDE_BID,
                        deal_id, m->stock, m->money);
    }

    mpd_sub(taker->left, taker->left, deal, &mpd_ctx);
    mpd_add(taker->deal_stock, taker->deal_stock, amount, &mpd_ctx);
    mpd_add(taker->deal_money, taker->deal_money, deal, &mpd_ctx);
    mpd_add(taker->deal_fee, taker->deal_fee, bid_fee, &mpd_ctx);

    balance_sub(taker->user_id, BALANCE_TYPE_AVAILABLE, m->money, deal);
    if (real) {
      append_balance_trade_sub(taker, m->money, deal, price, amount);
    }
    balance_add(taker->user_id, BALANCE_TYPE_AVAILABLE, m->stock, amount);
    if (real) {
      append_balance_trade_add(taker, m->stock, amount, price, amount);
    }
    if (mpd_cmp(bid_fee, mpd_zero, &mpd_ctx) > 0) {
      balance_sub(taker->user_id, BALANCE_TYPE_AVAILABLE, m->stock, bid_fee);
      if (real) {
        append_balance_trade_fee(taker, m->stock, bid_fee, price, amount,
                                 taker->taker_fee);
      }
    }

    mpd_sub(maker->left, maker->left, amount, &mpd_ctx);
    mpd_sub(maker->freeze, maker->freeze, amount, &mpd_ctx);
    mpd_add(maker->deal_stock, maker->deal_stock, amount, &mpd_ctx);
    mpd_add(maker->deal_money, maker->deal_money, deal, &mpd_ctx);
    mpd_add(maker->deal_fee, maker->deal_fee, ask_fee, &mpd_ctx);

    balance_sub(maker->user_id, BALANCE_TYPE_FREEZE, m->stock, amount);
    if (real) {
      append_balance_trade_sub(maker, m->stock, amount, price, amount);
    }
    balance_add(maker->user_id, BALANCE_TYPE_AVAILABLE, m->money, deal);
    if (real) {
      append_balance_trade_add(maker, m->money, deal, price, amount);
    }
    if (mpd_cmp(ask_fee, mpd_zero, &mpd_ctx) > 0) {
      balance_sub(maker->user_id, BALANCE_TYPE_AVAILABLE, m->money, ask_fee);
      if (real) {
        append_balance_trade_fee(maker, m->money, ask_fee, price, amount,
                                 maker->maker_fee);
      }
    }

    if (mpd_cmp(maker->left, mpd_zero, &mpd_ctx) == 0) {
      if (real) {
        push_order_message(ORDER_EVENT_FINISH, maker, m);
      }
      order_finish(real, m, maker);
    } else {
      if (real) {
        push_order_message(ORDER_EVENT_UPDATE, maker, m);
      }
    }
  }
  skiplist_release_iterator(iter);

  mpd_del(amount);
  mpd_del(price);
  mpd_del(deal);
  mpd_del(ask_fee);
  mpd_del(bid_fee);
  mpd_del(result);

  return 0;
}

//将市价订单加入撮合池
int market_put_market_order(bool real, json_t **result, market_t *m,
                            uint32_t user_id, uint32_t side, mpd_t *amount,
                            mpd_t *taker_fee, const char *source) {
  if (side == MARKET_ORDER_SIDE_ASK) {
    mpd_t *balance = balance_get(user_id, BALANCE_TYPE_AVAILABLE, m->stock);
    if (!balance || mpd_cmp(balance, amount, &mpd_ctx) < 0) {
      return -1;
    }

    skiplist_iter *iter = skiplist_get_iterator(m->bids);
    skiplist_node *node = skiplist_next(iter);
    if (node == NULL) {
      skiplist_release_iterator(iter);
      return -3;
    }
    skiplist_release_iterator(iter);

    if (mpd_cmp(amount, m->min_amount, &mpd_ctx) < 0) {
      return -2;
    }
  } else {
    mpd_t *balance = balance_get(user_id, BALANCE_TYPE_AVAILABLE, m->money);
    if (!balance || mpd_cmp(balance, amount, &mpd_ctx) < 0) {
      return -1;
    }

    skiplist_iter *iter = skiplist_get_iterator(m->asks);
    skiplist_node *node = skiplist_next(iter);
    if (node == NULL) {
      skiplist_release_iterator(iter);
      return -3;
    }
    skiplist_release_iterator(iter);

    order_t *order = node->value;
    mpd_t *require = mpd_new(&mpd_ctx);
    mpd_mul(require, order->price, m->min_amount, &mpd_ctx);
    if (mpd_cmp(amount, require, &mpd_ctx) < 0) {
      mpd_del(require);
      return -2;
    }
    mpd_del(require);
  }

  order_t *order = malloc(sizeof(order_t));
  if (order == NULL) {
    return -__LINE__;
  }

  order->id = ++order_id_start;
  order->type = MARKET_ORDER_TYPE_MARKET;
  order->side = side;
  order->create_time = current_timestamp();
  order->update_time = order->create_time;
  order->market = strdup(m->name);
  order->source = strdup(source);
  order->user_id = user_id;
  order->price = mpd_new(&mpd_ctx);
  order->amount = mpd_new(&mpd_ctx);
  order->taker_fee = mpd_new(&mpd_ctx);
  order->maker_fee = mpd_new(&mpd_ctx);
  order->left = mpd_new(&mpd_ctx);
  order->freeze = mpd_new(&mpd_ctx);
  order->deal_stock = mpd_new(&mpd_ctx);
  order->deal_money = mpd_new(&mpd_ctx);
  order->deal_fee = mpd_new(&mpd_ctx);

  mpd_copy(order->price, mpd_zero, &mpd_ctx);
  mpd_copy(order->amount, amount, &mpd_ctx);
  mpd_copy(order->taker_fee, taker_fee, &mpd_ctx);
  mpd_copy(order->maker_fee, mpd_zero, &mpd_ctx);
  mpd_copy(order->left, amount, &mpd_ctx);
  mpd_copy(order->freeze, mpd_zero, &mpd_ctx);
  mpd_copy(order->deal_stock, mpd_zero, &mpd_ctx);
  mpd_copy(order->deal_money, mpd_zero, &mpd_ctx);
  mpd_copy(order->deal_fee, mpd_zero, &mpd_ctx);

  int ret;
  if (side == MARKET_ORDER_SIDE_ASK) {
    ret = execute_market_ask_order(real, m, order);
  } else {
    ret = execute_market_bid_order(real, m, order);
  }
  if (ret < 0) {
    log_error("execute order: %" PRIu64 " fail: %d", order->id, ret);
    order_free(order);
    return -__LINE__;
  }

  if (real) {
    int ret = append_order_history(order);
    if (ret < 0) {
      log_fatal("append_order_history fail: %d, order: %" PRIu64 "", ret,
                order->id);
    }
    push_order_message(ORDER_EVENT_FINISH, order, m);
    *result = get_order_info(order);
  }

  order_free(order);
  return 0;
}

//取消订单
int market_cancel_order(bool real, json_t **result, market_t *m,
                        order_t *order) {
  if (real) {
    push_order_message(ORDER_EVENT_FINISH, order, m);
    *result = get_order_info(order);
  }
  order_finish(real, m, order);
  return 0;
}

int market_put_order(market_t *m, order_t *order) {
  return order_put(m, order);
}

//查询订单
order_t *market_get_order(market_t *m, uint64_t order_id) {

  //字典的键
  struct dict_order_key key = {.order_id = order_id};
  // 根据订单id从订单表里查找
  dict_entry *entry = dict_find(m->orders, &key);
  if (entry) {
    //如果找到，返回数据
    return entry->val;
  }
  return NULL;
}

//查询订单列表
skiplist_t *market_get_order_list(market_t *m, uint32_t user_id) {
  struct dict_user_key key = {.user_id = user_id};
  // 根据用户id从用户表里查找
  dict_entry *entry = dict_find(m->users, &key);
  if (entry) {
    return entry->val;
  }
  return NULL;
}

//查询交易对撮合池状态
int market_get_status(market_t *m, size_t *ask_count, mpd_t *ask_amount,
                      size_t *bid_count, mpd_t *bid_amount) {
  *ask_count = m->asks->len;
  *bid_count = m->bids->len;
  mpd_copy(ask_amount, mpd_zero, &mpd_ctx);
  mpd_copy(bid_amount, mpd_zero, &mpd_ctx);

  skiplist_node *node;
  skiplist_iter *iter = skiplist_get_iterator(m->asks);
  while ((node = skiplist_next(iter)) != NULL) {
    order_t *order = node->value;
    mpd_add(ask_amount, ask_amount, order->left, &mpd_ctx);
  }
  skiplist_release_iterator(iter);

  iter = skiplist_get_iterator(m->bids);
  while ((node = skiplist_next(iter)) != NULL) {
    order_t *order = node->value;
    mpd_add(bid_amount, bid_amount, order->left, &mpd_ctx);
  }

  return 0;
}

//交易对撮合池状态
sds market_status(sds reply) {
  reply = sdscatprintf(reply, "order last ID: %" PRIu64 "\n", order_id_start);
  reply = sdscatprintf(reply, "deals last ID: %" PRIu64 "\n", deals_id_start);
  return reply;
}
