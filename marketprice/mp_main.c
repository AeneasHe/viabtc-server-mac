/*
 * Description:
 *     History: yang@haipo.me, 2017/04/16, create
 */

#ifdef __APPLE__
#define error printf
#endif

#include "mp_config.h"
#include "mp_message.h"
#include "mp_server.h"

const char *__process__ = "marketprice";
const char *__version__ = "0.1.0";

nw_timer cron_timer;

static void on_cron_check(nw_timer *timer, void *data) {
  dlog_check_all();
  if (signal_exit) {
    nw_loop_break();
    signal_exit = 0;
  }
}

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

  int ret;

  // 高进度计算初始化
  ret = init_mpd();
  if (ret < 0) {
    error("init mpd fail: %d", ret);
  }

  // 配置初始化
  ret = init_config(argv[1]);
  if (ret < 0) {
    error("load config fail: %d", ret);
  }

  // 进程初始化
  ret = init_process();
  if (ret < 0) {
    error("init process fail: %d", ret);
  }

  ret = init_log();
  if (ret < 0) {
    error("init log fail: %d", ret);
  }

  //创建守护进程
  daemon(1, 1);
  //进程保活
  process_keepalive();

  // redis消息系统初始化：注意redis要以哨兵模式运行
  ret = init_message();
  if (ret < 0) {
    error("init message fail: %d", ret);
  }

  ret = init_server();
  if (ret < 0) {
    error("init server fail: %d", ret);
  }

  nw_timer_set(&cron_timer, 0.5, true, on_cron_check, NULL);
  nw_timer_start(&cron_timer);

  log_vip("server start");
  log_stderr("server start");
  nw_loop_run();
  log_vip("server stop");

  return 0;
}
