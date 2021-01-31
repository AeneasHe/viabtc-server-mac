# 交易对撮合引擎

## 1.说明
### a.配置模块
me_config.c     配置
### b.核心模块
me_main.c       主入口  
me_server.c     rpc服务器  
me_market.c     交易对撮合池  

### c.定义模块
me_balance.c    账户及资产定义  
me_trade.c      交易对定义  

### d.交互模块
me_update.c     更新记录：将撮合池中的信息更新到数据库及kafka  

me_message.c    kafka消息  
me_history.c    交易历史记录存入mysql  
me_operlog.c    操作历史记录存入mysql

me_persist.c    镜像持久化:将撮合池的信息保存为镜像以便保存或恢复  
me_dump.c       持久化:载出镜像
me_load.c       持久化:载入镜像  

me_cli.c        客户端命令模块

