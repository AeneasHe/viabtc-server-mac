# viabtc 在 mac 下的启动

# 运行环境

## mysql 配置和启动

### mysql 安装

版本：mysql 5.7

```bash
brew install mysql@5.7
brew services start mysql@5.7
```

安装目录: /usr/local/Cellar/mysql@5.7/5.7.25/bin

### mysql 配置

进入 mysql 客户端

```
mysql -u root -p
```

输入密码 mytaraxa2019

```sql
create database trade_history;
use trade_history;
source create_trade_history.sql;

create database trade_log;
use trade_log;
source create_trade_log.sql;
\q
```

回到控制台修改 sql/init_trade_history.sh 里的用户名密码

```bash
vi ./init_trade_history.sh
```

## redis 启动

```bash
brew services start redis
```

## kafka 启动

```bash
brew services start zookeeper
brew services start kafka
```

# Viabtc 子程序启动顺序

matchengine
alertcenter
readhistory
marketprice
accesshttp
accessws

ps -ax|grep matchengine
ps -ax|grep alertcenter
ps -ax|grep readhistory
ps -ax|grep accesshttp
ps -ax|grep accessws
ps -ax|grep marketprice
