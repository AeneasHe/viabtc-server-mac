/*
 * Description:
 *     History: yang@haipo.me, 2017/03/16, create
 */
# ifdef __APPLE__
# define error printf
# endif

# include "me_balance.h"
# include "me_cli.h"
# include "me_config.h"
# include "me_history.h"
# include "me_market.h"
# include "me_message.h"
# include "me_operlog.h"
# include "me_persist.h"
# include "me_server.h"
# include "me_trade.h"
# include "me_update.h"

const char *__process__ = "matchengine";
const char *__version__ = "0.1.0";

nw_timer cron_timer;

// cron查询事件
static void on_cron_check(nw_timer *timer, void *data) {
  dlog_check_all();
  if (signal_exit) {
    nw_loop_break();
    signal_exit = 0;
  }
}

// 初始化进程
static int init_process(void) {
  if (settings.process.file_limit) {
    if (set_file_limit(settings.process.file_limit) < 0) {
      return -__LINE__;
    }
  }
  if (settings.process.core_limit) {
    if (set_core_limit(settings.process.core_limit) < 0) {
      return -__LINE__;
    }
  }

  return 0;
}

// 初始化日志
static int init_log(void) {
  default_dlog =
      dlog_init(settings.log.path, settings.log.shift, settings.log.max,
                settings.log.num, settings.log.keep);
  if (default_dlog == NULL)
    return -__LINE__;
  default_dlog_flag = dlog_read_flag(settings.log.flag);
  if (alert_init(&settings.alert) < 0)
    return -__LINE__;

  return 0;
}

int main(int argc, char *argv[]) {

  // 打印进程启动消息
  printf("process: %s version: %s, compile date: %s %s\n", __process__,
         __version__, __DATE__, __TIME__);

  if (argc < 2) {
    printf("usage: %s config.json\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  if (process_exist(__process__) != 0) {
    printf("process: %s exist\n", __process__);
    exit(EXIT_FAILURE);
  }

  // 结果变量
  int ret;

  // 高精度计算初始化
  //../utils/ut_decimal.c
  ret = init_mpd();
  if (ret < 0) {
    error(EXIT_FAILURE, errno, "init mpd fail: %d", ret);
  }

  // 配置初始化
  // me_config.c
  ret = init_config(argv[1]);
  if (ret < 0) {
    error(EXIT_FAILURE, errno, "load config fail: %d", ret);
  }

  // 进程初始化
  // me_main.c
  ret = init_process();
  if (ret < 0) {
    error(EXIT_FAILURE, errno, "init process fail: %d", ret);
  }

  // 日志初始化
  // me_main.c
  ret = init_log();
  if (ret < 0) {
    error(EXIT_FAILURE, errno, "init log fail: %d", ret);
  }

  /*
  核心功能的初始化
   */

  // 账户余额初始化
  // me_balance.c
  ret = init_balance();
  if (ret < 0) {
    error(EXIT_FAILURE, errno, "init balance fail: %d", ret);
  }

  //更新初始化
  // me_update.c
  ret = init_update();
  if (ret < 0) {
    error(EXIT_FAILURE, errno, "init update fail: %d", ret);
  }

  // 交易对初始化
  // me_trade.c
  ret = init_trade();
  if (ret < 0) {
    error(EXIT_FAILURE, errno, "init trade fail: %d", ret);
  }

  daemon(1, 1);
  process_keepalive();

  /* 杂项 */

  // 从数据库保存的切片数据复原
  ret = init_from_db();
  if (ret < 0) {
    error(EXIT_FAILURE, errno, "init from db fail: %d", ret);
  }

  // 初始化操作历史
  // me_operlog.c
  ret = init_operlog();
  if (ret < 0) {
    error(EXIT_FAILURE, errno, "init oper log fail: %d", ret);
  }

  //初始化历史
  // me_history.c
  ret = init_history();
  if (ret < 0) {
    error(EXIT_FAILURE, errno, "init history fail: %d", ret);
  }

  // 初始化消息
  // me_message.c
  ret = init_message();
  if (ret < 0) {
    error(EXIT_FAILURE, errno, "init message fail: %d", ret);
  }

  // 初始化持久化
  // me_persist
  ret = init_persist();
  if (ret < 0) {
    error(EXIT_FAILURE, errno, "init persist fail: %d", ret);
  }

  // 初始化rpc客户端
  ret = init_cli();
  if (ret < 0) {
    error(EXIT_FAILURE, errno, "init cli fail: %d", ret);
  }

  // 初始化服务器
  ret = init_server();
  if (ret < 0) {
    error(EXIT_FAILURE, errno, "init server fail: %d", ret);
  }

  // 设定并开始计时器
  nw_timer_set(&cron_timer, 0.5, true, on_cron_check, NULL);
  nw_timer_start(&cron_timer);

  log_vip("server start");
  log_stderr("server start");
  nw_loop_run();
  log_vip("server stop");

  //
  fini_message();
  fini_history();
  fini_operlog();

  return 0;
}
