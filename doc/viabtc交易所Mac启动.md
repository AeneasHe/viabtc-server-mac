matchengine 正常
alertcenter 无法启动
readhistory 正常
accesshttp 能启动但没监听
accessws 无法启动
marketprice 能启动但没监听

mysql 启动

brew install mysql@5.7

brew services start mysql@5.7

安装目录/usr/local/Cellar/mysql@5.7/5.7.25/bin

redis 启动

brew services start redis

kafka 启动

brew services start zookeeper

brew services start kafka

mysql 配置

进入 mysql 客户端

mysql -u root -p

create database trade_history;

use trade_history;

source create_trade_history.sql;

create database trade_log;

use trade_log;

source create_trade_log.sql;

\q

回到控制台

先修改 init_trade_history.sh 里的用户名密码

./init_trade_history.sh

启动顺序

matchengine
alertcenter
readhistory
accesshttp
accessws
marketprice

ps -ax|grep matchengine

ps -ax|grep alertcenter

ps -ax|grep readhistory

ps -ax|grep accesshttp

ps -ax|grep accessws

ps -ax|grep marketprice
