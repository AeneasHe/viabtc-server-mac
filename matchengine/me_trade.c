/*
 * Description:
 *     History: yang@haipo.me, 2017/03/29, create
 */

# include "me_trade.h"
# include "me_config.h"

//核心对象： 交易对字典
static dict_t *dict_market;

//交易对字典定义相关函数
static uint32_t market_dict_hash_function(const void *key) {
  return dict_generic_hash_function(key, strlen(key));
}

static int market_dict_key_compare(const void *key1, const void *key2) {
  return strcmp(key1, key2);
}

static void *market_dict_key_dup(const void *key) { return strdup(key); }

static void market_dict_key_free(void *key) { free(key); }

//【程序入口】交易对初始化
int init_trade(void) {

  // 定义交易对字典
  dict_types type;
  memset(&type, 0, sizeof(type));
  type.hash_function = market_dict_hash_function;
  type.key_compare = market_dict_key_compare;
  type.key_dup = market_dict_key_dup;
  type.key_destructor = market_dict_key_free;

  //创建交易对字典
  dict_market = dict_create(&type, 64);
  if (dict_market == NULL)
    return -__LINE__;

  for (size_t i = 0; i < settings.market_num; ++i) {
    market_t *m = market_create(
        &settings.markets[i]); //载入配置文件，将交易对信息写入交易对字典
    if (m == NULL) {
      return -__LINE__;
    }

    dict_add(dict_market, settings.markets[i].name, m);
  }

  return 0;
}

//查询交易对
market_t *get_market(const char *name) {
  dict_entry *entry = dict_find(dict_market, name);
  if (entry)
    return entry->val;
  return NULL;
}
