#include "lwip/opt.h"
#include "lwip/apps/httpd.h"
#include "lwip/tcp.h"
#include "lwip/mem.h"
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "lwip/def.h"

#ifndef HTTPD_SERVER_PORT
#define HTTPD_SERVER_PORT 80
#endif

#define HTTP_SERVER_POLL_INTERVAL 4

struct http_session {
  u16_t remaining;
  u8_t response_sent;
};

static const char http_response[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 13\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Hello, world!";

static void http_session_free(struct http_session *session) {
  if (session != NULL) {
    mem_free(session);
  }
}

static err_t http_close(struct tcp_pcb *pcb, struct http_session *session) {
  err_t err = ERR_OK;

  tcp_arg(pcb, NULL);
  tcp_sent(pcb, NULL);
  tcp_recv(pcb, NULL);
  tcp_poll(pcb, NULL, 0);
  tcp_err(pcb, NULL);

  if (session != NULL) {
    http_session_free(session);
  }

  if (pcb != NULL) {
    err = tcp_close(pcb);
    if (err != ERR_OK) {
      tcp_abort(pcb);
      err = ERR_OK;
    }
  }

  return err;
}

static err_t http_server_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
  struct http_session *session = (struct http_session *)arg;

  if (session != NULL) {
    if (len >= session->remaining) {
      session->remaining = 0;
    } else {
      session->remaining = (u16_t)(session->remaining - len);
    }
    if (session->remaining == 0) {
      http_close(pcb, session);
    }
  }

  return ERR_OK;
}

static void http_server_err(void *arg, err_t err) {
  struct http_session *session = (struct http_session *)arg;
  LWIP_UNUSED_ARG(err);
  http_session_free(session);
}

static err_t http_server_poll(void *arg, struct tcp_pcb *pcb) {
  struct http_session *session = (struct http_session *)arg;
  if (session == NULL) {
    return http_close(pcb, NULL);
  }
  if ((session->response_sent != 0U) && (session->remaining == 0U)) {
    return http_close(pcb, session);
  }
  return ERR_OK;
}

static err_t http_server_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
  struct http_session *session = (struct http_session *)arg;

  if ((err != ERR_OK) || (p == NULL)) {
    if (p != NULL) {
      pbuf_free(p);
    }
    return http_close(pcb, session);
  }

  tcp_recved(pcb, p->tot_len);
  pbuf_free(p);

  if (session->remaining == 0) {
    session->remaining = (u16_t)(sizeof(http_response) - 1U);
    session->response_sent = 1U;
    err_t wr_err = tcp_write(pcb, http_response, session->remaining, TCP_WRITE_FLAG_COPY);
    if (wr_err != ERR_OK) {
      session->remaining = 0;
      return http_close(pcb, session);
    }
    tcp_output(pcb);
  }

  return ERR_OK;
}

static err_t http_server_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
  LWIP_UNUSED_ARG(arg);

  if ((err != ERR_OK) || (pcb == NULL)) {
    return ERR_VAL;
  }

  struct http_session *session = (struct http_session *)mem_malloc(sizeof(struct http_session));
  if (session == NULL) {
    http_close(pcb, NULL);
    return ERR_MEM;
  }

  session->remaining = 0;
  session->response_sent = 0U;
  tcp_arg(pcb, session);
  tcp_recv(pcb, http_server_recv);
  tcp_err(pcb, http_server_err);
  tcp_sent(pcb, http_server_sent);
  tcp_poll(pcb, http_server_poll, HTTP_SERVER_POLL_INTERVAL);

  return ERR_OK;
}

void httpd_init(void) {
  struct tcp_pcb *pcb = tcp_new();
  if (pcb == NULL) {
    return;
  }

  err_t err = tcp_bind(pcb, IP_ADDR_ANY, HTTPD_SERVER_PORT);
  if (err != ERR_OK) {
    tcp_abort(pcb);
    return;
  }

  struct tcp_pcb *listen_pcb = tcp_listen(pcb);
  if (listen_pcb == NULL) {
    tcp_abort(pcb);
    return;
  }

  tcp_accept(listen_pcb, http_server_accept);
}/*
 * httpd_custom.c
 *
 *  Created on: Nov 8, 2025
 *      Author: gabriele
 */


