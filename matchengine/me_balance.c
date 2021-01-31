/*
 * Description: 
 *     History: yang@haipo.me, 2017/03/15, create
 */

#include "me_config.h"
#include "me_balance.h"

//账户字典
dict_t *dict_balance;
//资产字典
static dict_t *dict_asset;
//资产类型
struct asset_type
{
    int prec_save; //存储精度
    int prec_show; //显示精度
};
//资产字典转hash
static uint32_t asset_dict_hash_function(const void *key)
{
    return dict_generic_hash_function(key, strlen(key));
}
//获取资产字典的key
static void *asset_dict_key_dup(const void *key)
{
    return strdup(key);
}
//获取资产字典的value
static void *asset_dict_val_dup(const void *val)
{
    struct asset_type *obj = malloc(sizeof(struct asset_type));
    if (obj == NULL)
        return NULL;
    memcpy(obj, val, sizeof(struct asset_type));
    return obj;
}
//比较资产字典的key
static int asset_dict_key_compare(const void *key1, const void *key2)
{
    return strcmp(key1, key2);
}
//释放资产字典的key
static void asset_dict_key_free(void *key)
{
    free(key);
}
//释放资产字典的value
static void asset_dict_val_free(void *val)
{
    free(val);
}

//将账户字典转hash
static uint32_t balance_dict_hash_function(const void *key)
{
    return dict_generic_hash_function(key, sizeof(struct balance_key));
}
//获取账户字典的key
static void *balance_dict_key_dup(const void *key)
{
    struct balance_key *obj = malloc(sizeof(struct balance_key));
    if (obj == NULL)
        return NULL;
    memcpy(obj, key, sizeof(struct balance_key));
    return obj;
}
//获取账户字典的value
static void *balance_dict_val_dup(const void *val)
{
    return mpd_qncopy(val);
}
//比较账户字典的key
static int balance_dict_key_compare(const void *key1, const void *key2)
{
    return memcmp(key1, key2, sizeof(struct balance_key));
}
//释放资产字典的key
static void balance_dict_key_free(void *key)
{
    free(key);
}
//释放资产字典的val
static void balance_dict_val_free(void *val)
{
    mpd_del(val);
}
//字典初始化
static int init_dict(void)
{
    dict_types type;
    memset(&type, 0, sizeof(type));
    type.hash_function = asset_dict_hash_function;
    type.key_compare = asset_dict_key_compare;
    type.key_dup = asset_dict_key_dup;
    type.key_destructor = asset_dict_key_free;
    type.val_dup = asset_dict_val_dup;
    type.val_destructor = asset_dict_val_free;

    dict_asset = dict_create(&type, 64); //资产字典,全局变量
    if (dict_asset == NULL)
        return -__LINE__;

    memset(&type, 0, sizeof(type));
    type.hash_function = balance_dict_hash_function;
    type.key_compare = balance_dict_key_compare;
    type.key_dup = balance_dict_key_dup;
    type.key_destructor = balance_dict_key_free;
    type.val_dup = balance_dict_val_dup;
    type.val_destructor = balance_dict_val_free;

    dict_balance = dict_create(&type, 64); //账户字典,全局变量
    if (dict_balance == NULL)
        return -__LINE__;

    return 0;
}

//账户初始化
int init_balance()
{
    ERR_RET(init_dict()); //资产字典和账户字典初始化

    for (size_t i = 0; i < settings.asset_num; ++i)
    {
        struct asset_type type;
        type.prec_save = settings.assets[i].prec_save;
        type.prec_show = settings.assets[i].prec_show;
        if (dict_add(dict_asset, settings.assets[i].name, &type) == NULL)
            return -__LINE__;
    } //向资产字典中添加配置的资产类型

    return 0;
}

//获取资产类型（根据字符，从资产字典查找对应的结构体）
static struct asset_type *get_asset_type(const char *asset)
{
    dict_entry *entry = dict_find(dict_asset, asset); //从资产字典中查找资产并返回一条记录
    if (entry == NULL)
        return NULL;

    return entry->val;
}
//资产字典中是否存在该类型资产
bool asset_exist(const char *asset)
{
    struct asset_type *at = get_asset_type(asset);
    return at ? true : false;
}
//获取资产的存储精度
int asset_prec(const char *asset)
{
    struct asset_type *at = get_asset_type(asset);
    return at ? at->prec_save : -1;
}
//获取资产的显示精度
int asset_prec_show(const char *asset)
{
    struct asset_type *at = get_asset_type(asset);
    return at ? at->prec_show : -1;
}
//获取账户字典中的余额（输入用户id,账户类型type,资产名称asset）
mpd_t *balance_get(uint32_t user_id, uint32_t type, const char *asset)
{
    struct balance_key key; //账户字典的key是包括三个user_id,tyoe,asset的一个结构体
    key.user_id = user_id;
    key.type = type;
    strncpy(key.asset, asset, sizeof(key.asset));

    dict_entry *entry = dict_find(dict_balance, &key); //查找该用户的该账户下该资产的余额
    if (entry)
    {
        return entry->val; //返回余额
    }

    return NULL;
}
//删除账户字典中的记录
void balance_del(uint32_t user_id, uint32_t type, const char *asset)
{
    struct balance_key key; //声明要删除的账户字典key
    key.user_id = user_id;
    key.type = type;
    strncpy(key.asset, asset, sizeof(key.asset));
    dict_delete(dict_balance, &key);
}
//设置账户字典中的余额
mpd_t *balance_set(uint32_t user_id, uint32_t type, const char *asset, mpd_t *amount)
{
    struct asset_type *at = get_asset_type(asset); //将字符的资产类型转成结构体
    if (at == NULL)
        return NULL;

    int ret = mpd_cmp(amount, mpd_zero, &mpd_ctx);
    if (ret < 0)
    {
        return NULL; //如果数量为负数，直接返回空
    }
    else if (ret == 0)
    {
        balance_del(user_id, type, asset);
        return mpd_zero; //如果为0，删除账户字典中的该记录
    }

    struct balance_key key; //声明要设置的账户字典key
    key.user_id = user_id;
    key.type = type;
    strncpy(key.asset, asset, sizeof(key.asset));

    mpd_t *result; //mpd是高精度的计算数学库，mpd_t高精度数值类型
    dict_entry *entry;
    entry = dict_find(dict_balance, &key); //查找账户字典中的该记录
    if (entry)                             //如果有记录
    {
        result = entry->val;
        mpd_rescale(result, amount, -at->prec_save, &mpd_ctx); //将result设置为amount,精度是该资产的存储精度
        return result;                                         //设置完成返回
    }

    entry = dict_add(dict_balance, &key, amount); //如果没有记录，就添加记录
    if (entry == NULL)
        return NULL;
    result = entry->val;
    mpd_rescale(result, amount, -at->prec_save, &mpd_ctx); //将result设置为amount,精度是该资产的存储精度

    return result;
}

//向账户字典中的增加余额
mpd_t *balance_add(uint32_t user_id, uint32_t type, const char *asset, mpd_t *amount)
{
    struct asset_type *at = get_asset_type(asset);
    if (at == NULL)
        return NULL;

    if (mpd_cmp(amount, mpd_zero, &mpd_ctx) < 0)
        return NULL;

    struct balance_key key;
    key.user_id = user_id;
    key.type = type;
    strncpy(key.asset, asset, sizeof(key.asset));

    mpd_t *result;
    dict_entry *entry = dict_find(dict_balance, &key); //查找账户字典中的该记录
    if (entry)                                         //如果有记录，则更新余额
    {
        result = entry->val;
        mpd_add(result, result, amount, &mpd_ctx); //精确加法
        mpd_rescale(result, result, -at->prec_save, &mpd_ctx);
        return result;
    }

    return balance_set(user_id, type, asset, amount); //如果没有记录，则直接设置余额
}

//从账户字典中的减少余额
mpd_t *balance_sub(uint32_t user_id, uint32_t type, const char *asset, mpd_t *amount)
{
    struct asset_type *at = get_asset_type(asset);
    if (at == NULL)
        return NULL;

    if (mpd_cmp(amount, mpd_zero, &mpd_ctx) < 0)
        return NULL;

    mpd_t *result = balance_get(user_id, type, asset);
    if (result == NULL)
        return NULL;
    if (mpd_cmp(result, amount, &mpd_ctx) < 0)
        return NULL;

    mpd_sub(result, result, amount, &mpd_ctx);
    if (mpd_cmp(result, mpd_zero, &mpd_ctx) == 0)
    {
        balance_del(user_id, type, asset);
        return mpd_zero;
    }
    mpd_rescale(result, result, -at->prec_save, &mpd_ctx);

    return result;
}
//冻结账户字典中的一定数量的余额
mpd_t *balance_freeze(uint32_t user_id, const char *asset, mpd_t *amount)
{
    struct asset_type *at = get_asset_type(asset);
    if (at == NULL)
        return NULL;

    if (mpd_cmp(amount, mpd_zero, &mpd_ctx) < 0)
        return NULL; //如果余额为0，直接返回
    mpd_t *available = balance_get(user_id, BALANCE_TYPE_AVAILABLE, asset);
    if (available == NULL)
        return NULL; //如果可用余额为空，直接返回
    if (mpd_cmp(available, amount, &mpd_ctx) < 0)
        return NULL; //如果可用余额小于要冻结的数量，直接返回

    if (balance_add(user_id, BALANCE_TYPE_FREEZE, asset, amount) == 0)
        return NULL;                                 //如果非冻结的余额加上要冻结的数量等于0
    mpd_sub(available, available, amount, &mpd_ctx); //可用余额减去要冻结的数量
    if (mpd_cmp(available, mpd_zero, &mpd_ctx) == 0) //如果可用余额为0，则删除该可用账户
    {
        balance_del(user_id, BALANCE_TYPE_AVAILABLE, asset);
        return mpd_zero;
    }
    mpd_rescale(available, available, -at->prec_save, &mpd_ctx);

    return available;
}
//解冻账户字典中的一定数量的余额
mpd_t *balance_unfreeze(uint32_t user_id, const char *asset, mpd_t *amount)
{
    struct asset_type *at = get_asset_type(asset);
    if (at == NULL)
        return NULL;

    if (mpd_cmp(amount, mpd_zero, &mpd_ctx) < 0)
        return NULL;
    mpd_t *freeze = balance_get(user_id, BALANCE_TYPE_FREEZE, asset);
    if (freeze == NULL)
        return NULL;
    if (mpd_cmp(freeze, amount, &mpd_ctx) < 0)
        return NULL;

    if (balance_add(user_id, BALANCE_TYPE_AVAILABLE, asset, amount) == 0)
        return NULL;
    mpd_sub(freeze, freeze, amount, &mpd_ctx);
    if (mpd_cmp(freeze, mpd_zero, &mpd_ctx) == 0)
    {
        balance_del(user_id, BALANCE_TYPE_FREEZE, asset);
        return mpd_zero;
    }
    mpd_rescale(freeze, freeze, -at->prec_save, &mpd_ctx);

    return freeze;
}
//计算账户字典中某账户的总资产
mpd_t *balance_total(uint32_t user_id, const char *asset)
{
    mpd_t *balance = mpd_new(&mpd_ctx);
    mpd_copy(balance, mpd_zero, &mpd_ctx);
    mpd_t *available = balance_get(user_id, BALANCE_TYPE_AVAILABLE, asset);
    if (available)
    {
        mpd_add(balance, balance, available, &mpd_ctx);
    }
    mpd_t *freeze = balance_get(user_id, BALANCE_TYPE_FREEZE, asset);
    if (freeze)
    {
        mpd_add(balance, balance, freeze, &mpd_ctx);
    }

    return balance;
}
//获取账户字典中所有账户的状态
int balance_status(const char *asset, mpd_t *total, size_t *available_count, mpd_t *available, size_t *freeze_count, mpd_t *freeze)
{
    *freeze_count = 0;
    *available_count = 0;
    mpd_copy(total, mpd_zero, &mpd_ctx);
    mpd_copy(freeze, mpd_zero, &mpd_ctx);
    mpd_copy(available, mpd_zero, &mpd_ctx);

    dict_entry *entry;
    dict_iterator *iter = dict_get_iterator(dict_balance);
    while ((entry = dict_next(iter)) != NULL)
    {
        struct balance_key *key = entry->key;
        if (strcmp(key->asset, asset) != 0)
            continue;
        mpd_add(total, total, entry->val, &mpd_ctx);
        if (key->type == BALANCE_TYPE_AVAILABLE)
        {
            *available_count += 1;
            mpd_add(available, available, entry->val, &mpd_ctx);
        }
        else
        {
            *freeze_count += 1;
            mpd_add(freeze, freeze, entry->val, &mpd_ctx);
        }
    }
    dict_release_iterator(iter);

    return 0;
}
