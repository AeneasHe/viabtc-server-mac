/*
 * Description:
 *     History: yang@haipo.me, 2017/04/21, create
 */

#ifdef __APPLE__
#define error printf
#endif

#include "ah_config.h"
#include "ah_listener.h"
#include "ah_server.h"
#include "ut_title.h"

const char *__process__ = "accesshttp";
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
  // 打印启动信息
  printf("process: %s version: %s, compile date: %s %s\n", __process__,
         __version__, __DATE__, __TIME__);

  // 启动命令参数没有配置文件时退出
  if (argc < 2) {
    printf("usage: %s config.json\n", argv[0]);
    exit(EXIT_FAILURE);
  }
  // 如果accesshttp进程不存在，打印退出消息
  if (process_exist(__process__) != 0) {
    printf("process: %s exist\n", __process__);
    exit(EXIT_FAILURE);
  }

  process_title_init(argc, argv);

  //结果变量
  int ret;

  // 1.配置初始化
  ret = init_config(argv[1]);
  if (ret < 0) {
    // error(EXIT_FAILURE, errno, "load config fail: %d", ret);
    printf("load config fail: %d", ret);
  }

  // 2.进程初始化
  ret = init_process();
  if (ret < 0) {
    // error(EXIT_FAILURE, errno, "init process fail: %d", ret);
    printf("init process fail: %d", ret);
  }

  // 3.日志初始化
  ret = init_log();
  if (ret < 0) {
    // error(EXIT_FAILURE, errno, "init log fail: %d", ret);
    printf("init log fail: %d", ret);
  }

  // 4.http_server初始化
  // 根据配置的worker数量，复制出相应的进程
  // server的作用是接收请求后，向marketprice,matchegine,readhistory查询，并将查询的数据返回给请求
  for (int i = 0; i < settings.worker_num; ++i) {
    int pid = fork(); //复制进程
    if (pid < 0) {
      // error(EXIT_FAILURE, errno, "fork error");
      printf("fork error");
    } else if (pid == 0) {
      process_title_set("%s_worker_%d", __process__, i);
      // daemon(1, 1);
      process_keepalive();
      if (i != 0) {
        dlog_set_no_shift(default_dlog);
      }

      // http_server初始化
      ret = init_server();
      if (ret < 0) {
        log_error("init server fail: %d", ret);
      }

      goto run;
    }
  }

  // 设定进程名称
  process_title_set("%s_listener", __process__);
  daemon(1, 1);

  // 使进程在后台运行
  process_keepalive();

  // 5.listener初始化
  // listener的作用是监听前端用户的请求，转发给worker
  ret = init_listener();
  // 开始监听消息
  if (ret < 0) {
    // error(EXIT_FAILURE, errno, "init listener fail: %d", ret);
    log_error("init listener fail: %d", ret);
  }
  dlog_set_no_shift(default_dlog);

run:
  // 设置计时器
  nw_timer_set(&cron_timer, 0.5, true, on_cron_check, NULL);
  // 启动计时器
  nw_timer_start(&cron_timer);

  log_vip("\n\nserver start\n");
  log_stderr("server start");
  nw_loop_run();
  log_vip("server stop");

  return 0;
}
