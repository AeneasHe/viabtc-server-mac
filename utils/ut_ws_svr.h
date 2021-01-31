/*
 * Description: Websocket server
 *              https://tools.ietf.org/html/rfc6455
 *     History: yang@haipo.me, 2017/04/26, create
 */

#ifndef _UT_WS_SVR_H_
#define _UT_WS_SVR_H_

#include "nw_buf.h"
#include "nw_svr.h"
#include "nw_timer.h"
#include "ut_http.h"

#define UT_WS_SVR_MAX_HEADER_SIZE 1024

typedef struct ws_svr_cfg {
  char *name;
  uint32_t bind_count;
  nw_svr_bind *bind_arr;
  uint32_t max_pkg_size;
  uint32_t buf_limit;
  uint32_t read_mem;
  uint32_t write_mem;
  int keep_alive;
  char *protocol;
  char *origin;
} ws_svr_cfg;

typedef struct ws_svr_type {
  void (*on_upgrade)(nw_ses *ses, const char *remote);
  void (*on_close)(nw_ses *ses, const char *remote);
  int (*on_message)(nw_ses *ses, const char *remote, const char *url,
                    void *message, size_t size);
  void *(*on_privdata_alloc)(void *svr);
  void (*on_privdata_free)(void *svr, void *privdata);
} ws_svr_type;

typedef struct ws_svr {
  char *name;
  nw_svr *raw_svr;
  nw_timer timer;
  nw_cache *privdata_cache;
  int keep_alive;
  char *protocol;
  char *origin;
  http_parser_settings settings;
  ws_svr_type type;
} ws_svr;

ws_svr *ws_svr_create(ws_svr_cfg *cfg, ws_svr_type *type);
int ws_svr_start(ws_svr *svr);
int ws_svr_stop(ws_svr *svr);
ws_svr *ws_svr_from_ses(nw_ses *ses);
void *ws_ses_privdata(nw_ses *ses);
int ws_send_text(nw_ses *ses, char *message);
int ws_send_binary(nw_ses *ses, void *data, size_t size);
int ws_svr_broadcast_text(ws_svr *svr, char *message);
int ws_svr_broadcast_binary(ws_svr *svr, void *data, size_t size);
void ws_svr_release(ws_svr *svr);
void ws_svr_close_clt(ws_svr *svr, nw_ses *ses);

#endif

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#include <machine/endian.h>

#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)

#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)

#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)

#define __BIG_ENDIAN BIG_ENDIAN
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#define __BYTE_ORDER BYTE_ORDER
#else
#include
#include
#endif