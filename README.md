## Compile and Install

### Compile on different platform

- [mac](https://github.com/cofepy/viabtc-server-mac)
- [ubuntu](https://github.com/cofepy/viabtc-server-ubuntu)

### Compile order

先编译 network,utils  
然后编译 accesshttp,accessws  
接着编译 marketprice,matchengine  
最后编译 readhistory，alertcenter

### Main services

network: 网络相关的基础函数  
utils: 各种工具类的基础函数

matchengine：这是最重要的部分，它记录用户余额并执行用户订单。 它在内存数据库中，将操作日志保存在 MySQL 中，并在启动时重做操作日志。 它还将用户历史记录写入 MySQL，增加余额，将订单和交易消息发送给 kafka。

marketprice：从 kafka 读取消息，并生成 k 行数据。

readhistory：从 MySQL 读取历史数据。

accesshttp：支持简单的 HTTP 接口，并隐藏了上层的复杂性。

accwssws：一种 Websocket 服务器，支持查询和推送用户和市场数据。 顺便说一句，您需要在前面使用 nginx 来支持 wss。

alertcenter：一个简单的服务器，它将致命级别的日志写入 redis 列表，以便我们可以发送警报电子邮件。

accesshttp: 接收 http 协议的请求，通过 rpc 调用得到结果，并响应请求  
accessws: 接收 websocket 协议的请求，通过 rpc 调用得到结果，并响应请求

matchengine: 核心模块，撮合引擎  
marketprice: 核心模块，市场行情  
readhistory: 核心模块， 读取历史数据

alertcenter： 消息通知

### Redis config

- marketprice
- alertcenter

redis 必需以哨兵模式运行:配置文件连接的是哨兵的地址，并注意配置主服务器集群的名字 name 要保持一致

参考文章 1:
[https://seanmcgary.com/posts/how-to-build-a-fault-tolerant-redis-cluster-with-sentinel/](https://seanmcgary.com/posts/how-to-build-a-fault-tolerant-redis-cluster-with-sentinel/)

参考文章 2:
https://www.jianshu.com/p/06ab9daf921d

### Some info

**Operating system**

Ubuntu 14.04 or Ubuntu 16.04. Not yet tested on other systems.

**Requirements**

See [requirements](https://github.com/viabtc/viabtc_exchange_server/wiki/requirements). Install the mentioned system or library.

You MUST use the depends/hiredis to install the hiredis library. Or it may not be compatible.

**Compilation**

Compile network and utils first. The rest all are independent.

**Deployment**

One single instance is given for matchengine, marketprice and alertcenter, while readhistory, accesshttp and accwssws can have multiple instances to work with loadbalancing.

Please do not install every instance on the same machine.

Every process runs in deamon and starts with a watchdog process. It will automatically restart within 1s when crashed.

The best practice of deploying the instance is in the following directory structure:

```
matchengine
|---bin
|   |---matchengine
|---log
|   |---matchengine.log
|---conf
|   |---config.json
|---shell
|   |---restart.sh
|   |---check_alive.sh
```

## API

[HTTP Protocol](https://github.com/viabtc/viabtc_exchange_server/wiki/HTTP-Protocol) and [Websocket Protocol](https://github.com/viabtc/viabtc_exchange_server/wiki/WebSocket-Protocol) documents are available in Chinese. Should time permit, we will have it translated into English in the future.

### Third-party Clients

- [Python3 API realisation](https://github.com/testnet-exchange/python-viabtc-api)
- [Ruby Gem 💎](https://github.com/krmbzds/viabtc)

## Websocket authorization

The websocket protocol has an authorization method (`server.auth`) which is used to authorize the websocket connection to subscribe to user specific events (trade and balance events).

To accomodate this method your exchange frontend will need to supply an internal endpoint which takes an authorization token from the HTTP header named `Authorization` and validates that token and returns the user_id.

The internal authorization endpoint is defined by the `auth_url` setting in the config file (`accessws/config.json`).

Example response: `{"code": 0, "message": null, "data": {"user_id": 1}}`

## Donation

- BTC/BCH: 1LB34q942fRN8ukMoaLJNWBjm5erZccgUb
- ETH: 0xd6938fcad9aa20de7360ce15090ec2e036867f27
