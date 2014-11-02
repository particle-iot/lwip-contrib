/**
 * @file
 * SMTP client module
 * 
 * Author: Simon Goldschmidt
 *
 * Example usage:
 *
 * void my_smtp_result_fn(void *arg, u8_t smtp_result, u16_t srv_err, err_t err)
 * {
 *   printf("mail (%p) sent with results: 0x%02x, 0x%04x, 0x%08x\n", arg,
 *          smtp_result, srv_err, err);
 * }
 * static void my_smtp_test(void)
 * {
 *   smtp_set_server_addr("mymailserver.org");
 *   -> set both username and password as NULL if no auth needed
 *   smtp_set_auth("username", "password");
 *   smtp_send_mail("sender", "recipient", "subject", "body", my_smtp_result_fn,
 *                  some_argument);
 * }
 *
 * When using from any other thread than the tcpip_thread (for NO_SYS==0), use
 * smtp_send_mail_int()!
 */

#include "lwip/opt.h"

#include "smtp.h"

#if LWIP_TCP
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"

#include <string.h>
#include <stdlib.h>

/** This is simple SMTP client for raw API.
 * It is a minimal implementation of SMTP as specified in RFC 5321.
 *
 * @todo:
 * - attachments (the main difficulty here is streaming base64-encoding to
 *   prevent having to allocate a buffer for the whole encoded file at once)
 * - test with more mail servers...
 */

/**
 * SMTP_DEBUG: Enable debugging for SNTP.
 */
#ifndef SMTP_DEBUG
#define SMTP_DEBUG                  LWIP_DBG_OFF
#endif

/** Maximum length reserved for server name */
#ifndef SMTP_MAX_SERVERNAME_LEN
#define SMTP_MAX_SERVERNAME_LEN 256
#endif

/** Maximum length reserved for username */
#ifndef SMTP_MAX_USERNAME_LEN
#define SMTP_MAX_USERNAME_LEN   32
#endif

/** Maximum length reserved for password */
#ifndef SMTP_MAX_PASS_LEN
#define SMTP_MAX_PASS_LEN       32
#endif

/** Set this to 0 if you know the authentication data will not change
 * during the smtp session, which saves some heap space. */
#ifndef SMTP_COPY_AUTHDATA
#define SMTP_COPY_AUTHDATA      1
#endif

/** Set this to 0 to save some code space if you know for sure that all data
 * passed to this module conforms to the requirements in the SMTP RFC.
 * WARNING: use this with care!
 */
#ifndef SMTP_CHECK_DATA
#define SMTP_CHECK_DATA         1
#endif

/** Set this to 1 to enable AUTH PLAIN support */
#ifndef SMTP_SUPPORT_AUTH_PLAIN
#define SMTP_SUPPORT_AUTH_PLAIN 1
#endif

/** Set this to 1 to enable AUTH LOGIN support */
#ifndef SMTP_SUPPORT_AUTH_LOGIN
#define SMTP_SUPPORT_AUTH_LOGIN 1
#endif

/** TCP poll interval. Unit is 0.5 sec. */
#define SMTP_POLL_INTERVAL      4
/** TCP poll timeout while sending message body, reset after every
 * successful write. 3 minutes */
#define SMTP_TIMEOUT_DATABLOCK  ( 3 * 60 * SMTP_POLL_INTERVAL / 2)
/** TCP poll timeout while waiting for confirmation after sending the body.
 * 10 minutes */
#define SMTP_TIMEOUT_DATATERM   (10 * 60 * SMTP_POLL_INTERVAL / 2)
/** TCP poll timeout while not sending the body.
 * This is somewhat lower than the RFC states (5 minutes for initial, MAIL
 * and RCPT) but still OK for us here.
 * 2 minutes */
#define SMTP_TIMEOUT            ( 2 * 60 * SMTP_POLL_INTERVAL / 2)

/* the various debug levels for this file */
#define SMTP_DEBUG_TRACE        (SMTP_DEBUG | LWIP_DBG_TRACE)
#define SMTP_DEBUG_STATE        (SMTP_DEBUG | LWIP_DBG_STATE)
#define SMTP_DEBUG_WARN         (SMTP_DEBUG | LWIP_DBG_LEVEL_WARNING)
#define SMTP_DEBUG_WARN_STATE   (SMTP_DEBUG | LWIP_DBG_LEVEL_WARNING | LWIP_DBG_STATE)
#define SMTP_DEBUG_SERIOUS      (SMTP_DEBUG | LWIP_DBG_LEVEL_SERIOUS)


#define SMTP_RX_BUF_LEN         255
#define SMTP_TX_BUF_LEN         255
#define SMTP_CRLF               "\r\n"
#define SMTP_CRLF_LEN           2

#define SMTP_RESP_220           "220"
#define SMTP_RESP_235           "235"
#define SMTP_RESP_250           "250"
#define SMTP_RESP_334           "334"
#define SMTP_RESP_354           "354"
#define SMTP_RESP_LOGIN_UNAME   "VXNlcm5hbWU6"
#define SMTP_RESP_LOGIN_PASS    "UGFzc3dvcmQ6"

#define SMTP_KEYWORD_AUTH_SP    "AUTH "
#define SMTP_KEYWORD_AUTH_EQ    "AUTH="
#define SMTP_KEYWORD_AUTH_LEN   5
#define SMTP_AUTH_PARAM_PLAIN   "PLAIN"
#define SMTP_AUTH_PARAM_LOGIN   "LOGIN"

#define SMTP_CMD_EHLO_1           "EHLO ["
#define SMTP_CMD_EHLO_1_LEN       6
#define SMTP_CMD_EHLO_2           "]\r\n"
#define SMTP_CMD_EHLO_2_LEN       3
#define SMTP_CMD_AUTHPLAIN_1      "AUTH PLAIN "
#define SMTP_CMD_AUTHPLAIN_1_LEN  11
#define SMTP_CMD_AUTHPLAIN_2      "\r\n"
#define SMTP_CMD_AUTHPLAIN_2_LEN  2
#define SMTP_CMD_AUTHLOGIN        "AUTH LOGIN\r\n"
#define SMTP_CMD_AUTHLOGIN_LEN    12
#define SMTP_CMD_MAIL_1           "MAIL FROM: <"
#define SMTP_CMD_MAIL_1_LEN       12
#define SMTP_CMD_MAIL_2           ">\r\n"
#define SMTP_CMD_MAIL_2_LEN       3
#define SMTP_CMD_RCPT_1           "RCPT TO: <"
#define SMTP_CMD_RCPT_1_LEN       10
#define SMTP_CMD_RCPT_2           ">\r\n"
#define SMTP_CMD_RCPT_2_LEN       3
#define SMTP_CMD_DATA             "DATA\r\n"
#define SMTP_CMD_DATA_LEN         6
#define SMTP_CMD_HEADER_1         "From: <"
#define SMTP_CMD_HEADER_1_LEN     7
#define SMTP_CMD_HEADER_2         ">\r\nTo: <"
#define SMTP_CMD_HEADER_2_LEN     8
#define SMTP_CMD_HEADER_3         ">\r\nSubject: "
#define SMTP_CMD_HEADER_3_LEN     12
#define SMTP_CMD_HEADER_4         "\r\n\r\n"
#define SMTP_CMD_HEADER_4_LEN     4
#define SMTP_CMD_BODY_FINISHED    "\r\n.\r\n"
#define SMTP_CMD_BODY_FINISHED_LEN 5
#define SMTP_CMD_QUIT             "QUIT\r\n"
#define SMTP_CMD_QUIT_LEN         6

#if SMTP_STAT_TX_BUF_MAX
#define SMTP_TX_BUF_MAX(len) LWIP_MACRO(if((len) > smtp_tx_buf_len_max) smtp_tx_buf_len_max = (len);)
#else /* SMTP_STAT_TX_BUF_MAX */
#define SMTP_TX_BUF_MAX(len)
#endif /* SMTP_STAT_TX_BUF_MAX */

#if SMTP_COPY_AUTHDATA
#define SMTP_USERNAME(session)        (session)->username
#define SMTP_PASS(session)            (session)->pass
#define SMTP_AUTH_PLAIN_DATA(session) (session)->auth_plain
#define SMTP_AUTH_PLAIN_LEN(session)  (session)->auth_plain_len
#else /* SMTP_COPY_AUTHDATA */
#define SMTP_USERNAME(session)        smtp_username
#define SMTP_PASS(session)            smtp_pass
#define SMTP_AUTH_PLAIN_DATA(session) smtp_auth_plain
#define SMTP_AUTH_PLAIN_LEN(session)  smtp_auth_plain_len
#endif /* SMTP_COPY_AUTHDATA */


/** State for SMTP client state machine */
enum smtp_session_state {
  SMTP_NULL,
  SMTP_HELO,
  SMTP_AUTH_PLAIN,
  SMTP_AUTH_LOGIN_UNAME,
  SMTP_AUTH_LOGIN_PASS,
  SMTP_AUTH_LOGIN,
  SMTP_MAIL,
  SMTP_RCPT,
  SMTP_DATA,
  SMTP_BODY,
  SMTP_QUIT,
  SMTP_CLOSED
};

#ifdef LWIP_DEBUG
/** State-to-string table for debugging */
const char *smtp_state_str[] = {
  "SMTP_NULL",
  "SMTP_HELO",
  "SMTP_AUTH_PLAIN",
  "SMTP_AUTH_LOGIN_UNAME",
  "SMTP_AUTH_LOGIN_PASS",
  "SMTP_AUTH_LOGIN",
  "SMTP_MAIL",
  "SMTP_RCPT",
  "SMTP_DATA",
  "SMTP_BODY",
  "SMTP_QUIT",
  "SMTP_CLOSED",
};

const char *smtp_result_strs[] = {
  "SMTP_RESULT_OK",
  "SMTP_RESULT_ERR_UNKNOWN",
  "SMTP_RESULT_ERR_CONNECT",
  "SMTP_RESULT_ERR_HOSTNAME",
  "SMTP_RESULT_ERR_CLOSED",
  "SMTP_RESULT_ERR_TIMEOUT",
  "SMTP_RESULT_ERR_SVR_RESP",
};
#endif /* LWIP_DEBUG */

/** struct keeping the body and state of an smtp session */
struct smtp_session {
  /** keeping the state of the smtp session */
  enum smtp_session_state state;
  /** timeout handling, if this reaches 0, the connection is closed */
  u16_t timer;
  /** helper buffer for transmit, not used for sending body */
  char tx_buf[SMTP_TX_BUF_LEN + 1];
  struct pbuf* p;
  /** source email address */
  const char* from;
  /** size of the sourceemail address */
  u16_t from_len;
  /** target email address */
  const char* to;
  /** size of the target email address */
  u16_t to_len;
  /** subject of the email */
  const char *subject;
  /** length of the subject string */
  u16_t subject_len;
  /** this is the body of the mail to be sent */
  const char* body;
  /** this is the length of the body to be sent */
  u16_t body_len;
  /** amount of data from body already sent */
  u16_t body_sent;
  /** callback function to call when closed */
  smtp_result_fn callback_fn;
  /** argument for callback function */
  void *callback_arg;
#if SMTP_COPY_AUTHDATA
  /** Username to use for this request */
  char *username;
  /** Password to use for this request */
  char *pass;
  /** Username and password combined as necessary for PLAIN authentication */
  char auth_plain[SMTP_MAX_USERNAME_LEN + SMTP_MAX_PASS_LEN + 3];
  /** Length of smtp_auth_plain string (cannot use strlen since it includes \0) */
  size_t auth_plain_len;
#endif /* SMTP_COPY_AUTHDATA */
};

/** IP address or DNS name of the server to use for next SMTP request */
static char smtp_server[SMTP_MAX_SERVERNAME_LEN + 1];
/** TCP port of the server to use for next SMTP request */
static u16_t smtp_server_port = SMTP_DEFAULT_PORT;
/** Username to use for the next SMTP request */
static char *smtp_username;
/** Password to use for the next SMTP request */
static char *smtp_pass;
/** Username and password combined as necessary for PLAIN authentication */
static char smtp_auth_plain[SMTP_MAX_USERNAME_LEN + SMTP_MAX_PASS_LEN + 3];
/** Length of smtp_auth_plain string (cannot use strlen since it includes \0) */
static size_t smtp_auth_plain_len;

static err_t  smtp_verify(const char *data, size_t data_len, u8_t linebreaks_allowed);
static err_t  smtp_tcp_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
static void   smtp_tcp_err(void *arg, err_t err);
static err_t  smtp_tcp_poll(void *arg, struct tcp_pcb *pcb);
static err_t  smtp_tcp_sent(void *arg, struct tcp_pcb *pcb, u16_t len);
static err_t  smtp_tcp_connected(void *arg, struct tcp_pcb *pcb, err_t err);
#if LWIP_DNS
static void   smtp_dns_found(const char* hostname, ip_addr_t *ipaddr, void *arg);
#endif /* LWIP_DNS */
static size_t smtp_base64_encode(char* target, size_t target_len, const char* source, size_t source_len);
static enum   smtp_session_state smtp_prepare_mail(struct smtp_session *s, u16_t *tx_buf_len);
static void   smtp_send_body(struct smtp_session *s, struct tcp_pcb *pcb);
static void   smtp_process(void *arg, struct tcp_pcb *pcb, struct pbuf *p);


#ifdef LWIP_DEBUG
/** Convert an smtp result to a string */
const char*
smtp_result_str(u8_t smtp_result)
{
  if (smtp_result > SMTP_RESULT_ERR_SVR_RESP) {
    return "UNKNOWN";
  }
  return smtp_result_strs[smtp_result];
}

/** Null-terminates the payload of p for printing out messages.
 * WARNING: use this only if p is not needed any more as the last byte of
 *          payload is deleted!
 */
static char*
smtp_pbuf_str(struct pbuf* p)
{
  if ((p == NULL) || (p->len == 0)) {
    return "";
  }
  ((char*)p->payload)[p->len] = 0;
  return p->payload;
}
#endif /* LWIP_DEBUG */

/** Set IP address or DNS name for next SMTP connection
 *
 * @param server IP address (in ASCII representation) or DNS name of the server
 */
void
smtp_set_server_addr(const char* server)
{
  size_t len = 0;
  if (server != NULL) {
    len = strlen(server);
  }
  if (len > SMTP_MAX_SERVERNAME_LEN) {
    len = SMTP_MAX_SERVERNAME_LEN;
  }
  memcpy(smtp_server, server, len);
}

/** Set TCP port for next SMTP connection
 *
 * @param port TCP port
 */
void
smtp_set_server_port(u16_t port)
{
  smtp_server_port = port;
}

/** Set authentication parameters for next SMTP connection
 *
 * @param username login name as passed to the server
 * @param pass password passed to the server together with username
 */
err_t
smtp_set_auth(const char* username, const char* pass)
{
  size_t uname_len = 0;
  size_t pass_len = 0;

  memset(smtp_auth_plain, 0xfa, 64);
  if (username != NULL) {
    uname_len = strlen(username);
    if (uname_len > SMTP_MAX_USERNAME_LEN) {
      LWIP_DEBUGF(SMTP_DEBUG_SERIOUS, ("Username is too long, %d instead of %d\n",
        (int)uname_len, SMTP_MAX_USERNAME_LEN));
      return ERR_ARG;
    }
  }
  if (pass != NULL) {
#if SMTP_SUPPORT_AUTH_LOGIN || SMTP_SUPPORT_AUTH_PLAIN
    pass_len = strlen(pass);
    if (pass_len > SMTP_MAX_PASS_LEN) {
      LWIP_DEBUGF(SMTP_DEBUG_SERIOUS, ("Password is too long, %d instead of %d\n",
        (int)uname_len, SMTP_MAX_USERNAME_LEN));
      return ERR_ARG;
    }
#else /* SMTP_SUPPORT_AUTH_LOGIN || SMTP_SUPPORT_AUTH_PLAIN */
    LWIP_DEBUGF(SMTP_DEBUG_WARN, ("Password not supported as no authentication methods are activated\n"));
#endif /* SMTP_SUPPORT_AUTH_LOGIN || SMTP_SUPPORT_AUTH_PLAIN */
  }
  *smtp_auth_plain = 0;
  if (username != NULL) {
    smtp_username = smtp_auth_plain + 1;
    strcpy(smtp_username, username);
  }
  if (pass != NULL) {
    smtp_pass = smtp_auth_plain + uname_len + 2;
    strcpy(smtp_pass, pass);
  }
  smtp_auth_plain_len = uname_len + pass_len + 2;

  return ERR_OK;
}

/** The actual mail-sending function, called by smtp_send_mail and
 * smtp_send_mail_static after setting up the struct smtp_session.
 */
static err_t
smtp_send_mail_alloced(struct smtp_session *s)
{
  err_t err;
  struct tcp_pcb* pcb;
  ip_addr_t addr;

  LWIP_ASSERT("no smtp_session supplied", s != NULL);

#if SMTP_CHECK_DATA
  /* check that body conforms to RFC:
   * - convert all single-CR or -LF in body to CRLF
   * - only 7-bit ASCII is allowed
   */
  if (smtp_verify(s->to, s->to_len, 0) != ERR_OK) {
    return ERR_ARG;
  }
  if (smtp_verify(s->from, s->from_len, 0) != ERR_OK) {
    return ERR_ARG;
  }
  if (smtp_verify(s->subject, s->subject_len, 0) != ERR_OK) {
    return ERR_ARG;
  }
  if (smtp_verify(s->body, s->body_len, 0) != ERR_OK) {
    return ERR_ARG;
  }
#endif /* SMTP_CHECK_DATA */

  pcb = tcp_new();
  if (pcb == NULL) {
    err = ERR_MEM;
    goto leave;
  }

#if SMTP_COPY_AUTHDATA
  /* copy auth data, ensuring the first byte is always zero */
  memcpy(s->auth_plain + 1, smtp_auth_plain + 1, smtp_auth_plain_len - 1);
  s->auth_plain_len = smtp_auth_plain_len;
  /* default username and pass is empty string */
  s->username = s->auth_plain;
  s->pass = s->auth_plain;
  if (smtp_username != NULL) {
    s->username += smtp_username - smtp_auth_plain;
  }
  if (smtp_pass != NULL) {
    s->pass += smtp_pass - smtp_auth_plain;
  }
#endif /* SMTP_COPY_AUTHDATA */

  s->state = SMTP_NULL;
  s->timer = SMTP_TIMEOUT;

  tcp_arg(pcb, s);
  tcp_recv(pcb, smtp_tcp_recv);
  tcp_err(pcb, smtp_tcp_err);
  tcp_poll(pcb, smtp_tcp_poll, SMTP_POLL_INTERVAL);
  tcp_sent(pcb, smtp_tcp_sent);

#if LWIP_DNS
  err = dns_gethostbyname(smtp_server, &addr, smtp_dns_found, pcb);
#else /* LWIP_DNS */
  addr.addr = ipaddr_addr(smtp_server);
  err = addr.addr == IPADDR_NONE ? ERR_ARG : ERR_OK;
#endif /* LWIP_DNS */
  if (err == ERR_OK) {
    err = tcp_connect(pcb, &addr, smtp_server_port, smtp_tcp_connected);
    if (err != ERR_OK) {
      LWIP_DEBUGF(SMTP_DEBUG_WARN_STATE, ("tcp_connect failed: %d\n", (int)err));
      goto deallocate_and_leave;
    }
  } else if (err != ERR_INPROGRESS) {
    LWIP_DEBUGF(SMTP_DEBUG_WARN_STATE, ("dns_gethostbyname failed: %d\n", (int)err));
    goto deallocate_and_leave;
  }
  return ERR_OK;

deallocate_and_leave:
  if (pcb != NULL) {
    tcp_arg(pcb, NULL);
    tcp_close(pcb);
  }
leave:
  mem_free(s);
  /* no need to call the callback here since we return != ERR_OK */
  return err;
}

/** Send an email via the currently selected server, username and password.
 *
 * @param from source email address (must be NULL-terminated)
 * @param to target email address (must be NULL-terminated)
 * @param subject email subject (must be NULL-terminated)
 * @param body email body (must be NULL-terminated)
 * @param 
 * @returns - ERR_OK if structures were allocated and no error occured starting the connection
 *            (this does not mean the email has been successfully sent!)
 *          - another err_t on error.
 */
err_t
smtp_send_mail(const char* from, const char* to, const char* subject, const char* body,
               smtp_result_fn callback_fn, void* callback_arg)
{
  struct smtp_session* s;
  size_t from_len = strlen(from);
  size_t to_len = strlen(to);
  size_t subject_len = strlen(subject);
  size_t body_len = strlen(body);
  size_t mem_len = sizeof(struct smtp_session);

  mem_len += from_len + to_len + subject_len + body_len + 4;
  if (mem_len > 0xffff) {
    /* too long! */
    return ERR_MEM;
  }

  /* Allocate memory to keep this email's session state */
  s = (struct smtp_session *)mem_malloc((mem_size_t)mem_len);
  if (s == NULL) {
    return ERR_MEM;
  }
  /* initialize the structure */
  memset(s, 0, mem_len);
  s->from = (char*)s + sizeof(struct smtp_session);
  s->from_len = (u16_t)from_len;
  s->to = s->from + from_len + 1;
  s->to_len = (u16_t)to_len;
  s->subject = s->to + to_len + 1;
  s->subject_len = (u16_t)subject_len;
  s->body = s->subject + subject_len + 1;
  s->body_len = (u16_t)body_len;
  /* copy source and target email address */
  memcpy((char*)s->from, from, from_len + 1);
  memcpy((char*)s->to, to, to_len + 1);
  memcpy((char*)s->subject, subject, subject_len + 1);
  memcpy((char*)s->body, body, body_len + 1);

  s->callback_fn = callback_fn;
  s->callback_arg = callback_arg;

  /* call the actual implementation of this function */
  return smtp_send_mail_alloced(s);
}

/** Same as smtp_send_mail, but doesn't copy from, to, subject and body into
 * an internal buffer to save memory.
 * WARNING: the above data must stay untouched until the callback function is
 *          called (unless the function returns != ERR_OK)
 */
err_t
smtp_send_mail_static(const char *from, const char* to, const char* subject,
  const char* body, smtp_result_fn callback_fn, void* callback_arg)
{
  struct smtp_session* s;
  size_t len;

  s = (struct smtp_session*)mem_malloc(sizeof(struct smtp_session));
  if (s == NULL) {
    return ERR_MEM;
  }
  memset(s, 0, sizeof(struct smtp_session));
  /* initialize the structure */
  s->from = from;
  len = strlen(from);
  LWIP_ASSERT("string is too long", len <= 0xffff);
  s->from_len = (u16_t)len;
  s->to = to;
  len = strlen(to);
  LWIP_ASSERT("string is too long", len <= 0xffff);
  s->to_len = (u16_t)len;
  s->subject = subject;
  len = strlen(subject);
  LWIP_ASSERT("string is too long", len <= 0xffff);
  s->subject_len = (u16_t)len;
  s->body = body;
  len = strlen(body);
  LWIP_ASSERT("string is too long", len <= 0xffff);
  s->body_len = (u16_t)len;
  s->callback_fn = callback_fn;
  s->callback_arg = callback_arg;
  /* call the actual implementation of this function */
  return smtp_send_mail_alloced(s);
}


/** Same as smpt_send_mail but takes a struct smtp_send_request as single
 * parameter which contains all the other parameters.
 * To be used with tcpip_callback to send mail from interrupt context or from
 * another thread.
 *
 * WARNING: server and authentication must stay untouched until this function has run!
 *
 * Usage example:
 * - allocate a struct smtp_send_request (in a way that is allowed in interrupt context)
 * - fill the members of the struct as if calling smtp_send_mail
 * - specify a callback_function
 * - set callback_arg to the structure itself
 * - call this function
 * - wait for the callback function to be called
 * - in the callback function, deallocate the structure (passed as arg)
 */
void
smtp_send_mail_int(void *arg)
{
  struct smtp_send_request *req = (struct smtp_send_request*)arg;
  err_t err;

  LWIP_ASSERT("smtp_send_mail_int: no argument given", arg != NULL);

  if (req->static_data) {
    err = smtp_send_mail_static(req->from, req->to, req->subject, req->body,
      req->callback_fn, req->callback_arg);
  } else {
    err = smtp_send_mail(req->from, req->to, req->subject, req->body,
      req->callback_fn, req->callback_arg);
  }
  if ((err != ERR_OK) && (req->callback_fn != NULL)) {
    req->callback_fn(req->callback_arg, SMTP_RESULT_ERR_UNKNOWN, 0, err);
  }
}

#if SMTP_CHECK_DATA
/** Verify that a given string conforms to the SMTP rules
 * (7-bit only, no single CR or LF,
 *  @todo: no line consisting of a single dot only)
 */
static err_t
smtp_verify(const char *data, size_t data_len, u8_t linebreaks_allowed)
{
  size_t i;
  u8_t last_was_cr = 0;
  for (i = 0; i < data_len; i++) {
    char current = data[i];
    if ((current & 0x80) != 0) {
      LWIP_DEBUGF(SMTP_DEBUG_WARN, ("smtp_verify: no 8-bit data supported: %s\n", data));
      return ERR_ARG;
    }
    if (current == '\r') {
      if (!linebreaks_allowed) {
        LWIP_DEBUGF(SMTP_DEBUG_WARN, ("smtp_verify: found CR where no linebreaks allowed: %s\n", data));
        return ERR_ARG;
      }
      if (last_was_cr) {
        LWIP_DEBUGF(SMTP_DEBUG_WARN, ("smtp_verify: found double CR: %s\n", data));
        return ERR_ARG;
      }
      last_was_cr = 1;
    } else {
      if (current == '\n') {
        if (!last_was_cr) {
          LWIP_DEBUGF(SMTP_DEBUG_WARN, ("smtp_verify: found LF without CR before: %s\n", data));
          return ERR_ARG;
        }
      }
      last_was_cr = 0;
    }
  }
  return ERR_OK;
}
#endif /* SMTP_CHECK_DATA */

/** Frees the smpt_session and calls the callback function */
static void
smtp_free(struct smtp_session *s, u8_t result, u16_t srv_err, err_t err)
{
  smtp_result_fn fn = s->callback_fn;
  void *arg = s->callback_arg;
  if (s->p != NULL) {
    pbuf_free(s->p);
  }
  mem_free(s);
  if (fn != NULL) {
    fn(arg, result, srv_err, err);
  }
}

/** Try to close a pcb and free the arg if successful */
static void
smtp_close(struct smtp_session *s, struct tcp_pcb *pcb, u8_t result,
           u16_t srv_err, err_t err)
{
  tcp_arg(pcb, NULL);
  if (tcp_close(pcb) == ERR_OK) {
    if (s != NULL) {
      smtp_free(s, result, srv_err, err);
    }
  } else {
    /* close failed, set back arg */
    tcp_arg(pcb, s);
  }
}

/** Raw API TCP err callback: pcb is already deallocated */
static void
smtp_tcp_err(void *arg, err_t err)
{
  LWIP_UNUSED_ARG(err);
  if (arg != NULL) {
    LWIP_DEBUGF(SMTP_DEBUG_WARN_STATE, ("smtp_tcp_err: connection reset by remote host\n"));
    smtp_free((struct smtp_session*)arg, SMTP_RESULT_ERR_CLOSED, 0, err);
  }
}

/** Raw API TCP poll callback */
static err_t
smtp_tcp_poll(void *arg, struct tcp_pcb *pcb)
{
  if (arg != NULL) {
    struct smtp_session *s = (struct smtp_session*)arg;
    if (s->timer != 0) {
      s->timer--;
    }
  }
  smtp_process(arg, pcb, NULL);
  return ERR_OK;
}

/** Raw API TCP sent callback */
static err_t
smtp_tcp_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
  LWIP_UNUSED_ARG(len);

  smtp_process(arg, pcb, NULL);

  return ERR_OK;
}

/** Raw API TCP recv callback */
static err_t
smtp_tcp_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
  LWIP_UNUSED_ARG(err);
  if (p != NULL) {
    tcp_recved(pcb, p->tot_len);
    smtp_process(arg, pcb, p);
  } else {
    LWIP_DEBUGF(SMTP_DEBUG_WARN_STATE, ("smtp_tcp_recv: connection closed by remote host\n"));
    smtp_close(arg, pcb, SMTP_RESULT_ERR_CLOSED, 0, err);
  }
  return ERR_OK;
}

static err_t
smtp_tcp_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
  LWIP_UNUSED_ARG(arg);

  if (err == ERR_OK) {
    LWIP_DEBUGF(SMTP_DEBUG_STATE, ("smtp_connected: Waiting for 220\n"));
  } else {
    /* shouldn't happen, but we still check 'err', only to be sure */
    LWIP_DEBUGF(SMTP_DEBUG_WARN, ("smtp_connected: %d\n", (int)err));
    smtp_close(arg, pcb, SMTP_RESULT_ERR_CONNECT, 0, err);
  }
  return ERR_OK;
}

#if LWIP_DNS
/** DNS callback
 * If ipaddr is non-NULL, resolving succeeded, otherwise it failed.
 */
static void
smtp_dns_found(const char* hostname, ip_addr_t *ipaddr, void *arg)
{
  struct tcp_pcb *pcb = arg;
  err_t err;
  u8_t result;

  LWIP_UNUSED_ARG(hostname);

  if (ipaddr != NULL) {
    LWIP_DEBUGF(SMTP_DEBUG_STATE, ("smtp_dns_found: hostname resolved, connecting\n"));
    err = tcp_connect(pcb, ipaddr, smtp_server_port, smtp_tcp_connected);
    if (err == ERR_OK) {
      return;
    }
    LWIP_DEBUGF(SMTP_DEBUG_WARN_STATE, ("tcp_connect failed: %d\n", (int)err));
    result = SMTP_RESULT_ERR_CONNECT;
  } else {
    LWIP_DEBUGF(SMTP_DEBUG_WARN_STATE, ("smtp_dns_found: failed to resolve hostname: %s\n",
      hostname));
    result = SMTP_RESULT_ERR_HOSTNAME;
    err = ERR_ARG;
  }
  smtp_close(pcb->callback_arg, pcb, result, 0, err);
}
#endif /* LWIP_DNS */

#if SMTP_SUPPORT_AUTH_PLAIN || SMTP_SUPPORT_AUTH_LOGIN

/** Table 6-bit-index-to-ASCII used for base64-encoding */
const u8_t base64_table[] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
  'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
  'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
  'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
  'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
  '+', '/'
};

/** Base64 encoding */
static size_t
smtp_base64_encode(char* target, size_t target_len, const char* source, size_t source_len)
{
  size_t i;
  s8_t j;
  size_t target_idx = 0;
  size_t longer = (source_len % 3) ? (3 - (source_len % 3)) : 0;
  size_t source_len_b64 = source_len + longer;
  size_t len = (((source_len_b64) * 4) / 3);
  u8_t x = 5;
  u8_t current = 0;
  LWIP_UNUSED_ARG(target_len);

  LWIP_ASSERT("target_len is too short", target_len >= len);

  for (i = 0; i < source_len_b64; i++) {
    u8_t b = (i < source_len ? source[i] : 0);
    for (j = 7; j >= 0; j--, x--) {
      u8_t shift = ((b & (1 << j)) != 0) ? 1 : 0;
      current |= shift << x;
      if (x == 0) {
        target[target_idx++] = base64_table[current];
        x = 6;
        current = 0;
      }
    }
  }
  for (i = len - longer; i < len; i++) {
    target[i] = '=';
  }
  return len;
}
#endif /* SMTP_SUPPORT_AUTH_PLAIN || SMTP_SUPPORT_AUTH_LOGIN */

/** Parse pbuf to see if it contains the beginning of an answer.
 * If so, it returns the contained response code as number between 1 and 999.
 * If not, zero is returned.
 *
 * @param s smtp session struct
 */
static u16_t
smtp_is_response(struct smtp_session *s)
{
  char digits[4];
  long num;

  if (s->p == NULL) {
    return 0;
  }
  /* copy three digits and convert them to int */
  if (pbuf_copy_partial(s->p, digits, 3, 0) != 3) {
    /* pbuf was too short */
    return 0;
  }
  digits[3] = 0;
  num = strtol(digits, NULL, 10);
  if ((num <= 0) || (num >= 1000)) {
    /* failed to find response code at start of line */
    return 0;
  }
  return (u16_t)num;
}

/** Parse pbuf to see if it contains a fully received answer.
 * If one is found, ERR_OK is returned.
 * If none is found, ERR_VAL is returned.
 *
 * A fully received answer is a 3-digit number followed by a space,
 * some string and a CRLF as line ending.
 *
 * @param s smtp session struct
 */
static err_t
smtp_is_response_finished(struct smtp_session *s)
{
  u8_t sp;
  u16_t crlf;
  u16_t offset;

  if (s->p == NULL) {
    return ERR_VAL;
  }
  offset = 0;
again:
  /* We could check the response number here, but we trust the
   * protocol definition which says the client can rely on it being
   * the same on every line. */

  /* find CRLF */
  crlf = pbuf_memfind(s->p, SMTP_CRLF, SMTP_CRLF_LEN, offset + 4);
  if (crlf == 0xFFFF) {
    /* no CRLF found */
    return ERR_VAL;
  }
  sp = pbuf_get_at(s->p, offset + 3);
  if (sp == '-') {
    /* no space after response code -> try next line */
    offset = crlf + 2;
    goto again;
  } else if (sp == ' ') {
    /* CRLF found after response code + space -> valid response */
    return ERR_OK;
  }
  /* sp contains invalid character */
  return ERR_VAL;
}

/** Prepare HELO/EHLO message */
static enum smtp_session_state
smtp_prepare_helo(struct smtp_session *s, u16_t *tx_buf_len, struct tcp_pcb *pcb)
{
  size_t ipa_len;
  char *ipa = ip_ntoa(&pcb->local_ip);
  LWIP_ASSERT("ip_ntoa returned NULL", ipa != NULL);
  ipa_len = strlen(ipa);
  LWIP_ASSERT("string too long", ipa_len <= 0xffff);

  *tx_buf_len = SMTP_CMD_EHLO_1_LEN + (u16_t)ipa_len + SMTP_CMD_EHLO_2_LEN;
  LWIP_ASSERT("tx_buf overflow detected", *tx_buf_len <= SMTP_TX_BUF_LEN);

  SMEMCPY(s->tx_buf, SMTP_CMD_EHLO_1, SMTP_CMD_EHLO_1_LEN);
  MEMCPY(&s->tx_buf[SMTP_CMD_EHLO_1_LEN], ipa, ipa_len);
  SMEMCPY(&s->tx_buf[SMTP_CMD_EHLO_1_LEN + ipa_len], SMTP_CMD_EHLO_2, SMTP_CMD_EHLO_2_LEN);
  return SMTP_HELO;
}

#if SMTP_SUPPORT_AUTH_PLAIN || SMTP_SUPPORT_AUTH_LOGIN
/** Parse last server response (in rx_buf) for supported authentication method,
 * create data to send out (to tx_buf), set tx_data_len correctly
 * and return the next state.
 */
static enum smtp_session_state
smtp_prepare_auth_or_mail(struct smtp_session *s, u16_t *tx_buf_len)
{
  /* check response for supported authentication method */
  u16_t auth = pbuf_strstr(s->p, SMTP_KEYWORD_AUTH_SP);
  if (auth == 0xFFFF) {
    auth = pbuf_strstr(s->p, SMTP_KEYWORD_AUTH_EQ);
  }
  if (auth != 0xFFFF) {
    u16_t crlf = pbuf_memfind(s->p, SMTP_CRLF, SMTP_CRLF_LEN, auth);
    if ((crlf != 0xFFFF) && (crlf > auth)) {
      /* use tx_buf temporarily */
      u16_t copied = pbuf_copy_partial(s->p, s->tx_buf, crlf - auth, auth);
      if (copied != 0) {
        char *sep = s->tx_buf + SMTP_KEYWORD_AUTH_LEN;
        s->tx_buf[copied] = 0;
#if SMTP_SUPPORT_AUTH_PLAIN
        /* favour PLAIN over LOGIN since it involves less requests */
        if (strstr(sep, SMTP_AUTH_PARAM_PLAIN) != NULL) {
          size_t auth_len;
          /* server supports AUTH PLAIN */
          SMEMCPY(s->tx_buf, SMTP_CMD_AUTHPLAIN_1, SMTP_CMD_AUTHPLAIN_1_LEN);

          /* add base64-encoded string "\0username\0password" */
          auth_len = smtp_base64_encode(&s->tx_buf[SMTP_CMD_AUTHPLAIN_1_LEN],
            SMTP_TX_BUF_LEN - SMTP_CMD_AUTHPLAIN_1_LEN, SMTP_AUTH_PLAIN_DATA(s),
            SMTP_AUTH_PLAIN_LEN(s));
          LWIP_ASSERT("string too long", auth_len <= 0xffff);
         *tx_buf_len = SMTP_CMD_AUTHPLAIN_1_LEN + SMTP_CMD_AUTHPLAIN_2_LEN + (u16_t)auth_len;
          LWIP_ASSERT("tx_buf overflow detected", *tx_buf_len <= SMTP_TX_BUF_LEN);
          SMEMCPY(&s->tx_buf[SMTP_CMD_AUTHPLAIN_1_LEN + auth_len], SMTP_CMD_AUTHPLAIN_2,
            SMTP_CMD_AUTHPLAIN_2_LEN);
          return SMTP_AUTH_PLAIN;
        } else
#endif /* SMTP_SUPPORT_AUTH_PLAIN */
        {
#if SMTP_SUPPORT_AUTH_LOGIN
          if (strstr(sep, SMTP_AUTH_PARAM_LOGIN) != NULL) {
            /* server supports AUTH LOGIN */
            *tx_buf_len = SMTP_CMD_AUTHLOGIN_LEN;
            SMEMCPY(s->tx_buf, SMTP_CMD_AUTHLOGIN, SMTP_CMD_AUTHLOGIN_LEN);
            return SMTP_AUTH_LOGIN_UNAME;
          }
#endif /* SMTP_SUPPORT_AUTH_LOGIN */
        }
      }
    }
  }
  /* server didnt's send correct keywords for AUTH, try sending directly */
  return smtp_prepare_mail(s, tx_buf_len);
}
#endif /* SMTP_SUPPORT_AUTH_PLAIN || SMTP_SUPPORT_AUTH_LOGIN */

#if SMTP_SUPPORT_AUTH_LOGIN
/** Send base64-encoded username */
static enum smtp_session_state
smtp_prepare_auth_login_uname(struct smtp_session *s, u16_t *tx_buf_len)
{
  size_t base64_len = smtp_base64_encode(s->tx_buf, SMTP_TX_BUF_LEN,
    SMTP_USERNAME(s), strlen(SMTP_USERNAME(s)));
  /* @todo: support base64-encoded longer than 64k */
  LWIP_ASSERT("string too long", base64_len <= 0xffff);
  LWIP_ASSERT("tx_buf overflow detected", base64_len + SMTP_CRLF_LEN <= SMTP_TX_BUF_LEN);
  *tx_buf_len = (u16_t)base64_len + SMTP_CRLF_LEN;

  SMEMCPY(&s->tx_buf[base64_len], SMTP_CRLF, SMTP_CRLF_LEN);
  s->tx_buf[*tx_buf_len] = 0;
  return SMTP_AUTH_LOGIN_PASS;
}

/** Send base64-encoded password */
static enum smtp_session_state
smtp_prepare_auth_login_pass(struct smtp_session *s, u16_t *tx_buf_len)
{
  size_t base64_len = smtp_base64_encode(s->tx_buf, SMTP_TX_BUF_LEN,
    SMTP_PASS(s), strlen(SMTP_PASS(s)));
  /* @todo: support base64-encoded longer than 64k */
  LWIP_ASSERT("string too long", base64_len <= 0xffff);
  LWIP_ASSERT("tx_buf overflow detected", base64_len + SMTP_CRLF_LEN <= SMTP_TX_BUF_LEN);
  *tx_buf_len = (u16_t)base64_len + SMTP_CRLF_LEN;

  SMEMCPY(&s->tx_buf[base64_len], SMTP_CRLF, SMTP_CRLF_LEN);
  s->tx_buf[*tx_buf_len] = 0;
  return SMTP_AUTH_LOGIN;
}
#endif /* SMTP_SUPPORT_AUTH_LOGIN */

/** Prepare MAIL message */
static enum smtp_session_state
smtp_prepare_mail(struct smtp_session *s, u16_t *tx_buf_len)
{
  char *target = s->tx_buf;
  *tx_buf_len = SMTP_CMD_MAIL_1_LEN + SMTP_CMD_MAIL_2_LEN + s->from_len;
  LWIP_ASSERT("tx_buf overflow detected", *tx_buf_len <= SMTP_TX_BUF_LEN);
  target[*tx_buf_len] = 0;

  SMEMCPY(target, SMTP_CMD_MAIL_1, SMTP_CMD_MAIL_1_LEN);
  target += SMTP_CMD_MAIL_1_LEN;
  memcpy(target, s->from, s->from_len);
  target += s->from_len;
  SMEMCPY(target, SMTP_CMD_MAIL_2, SMTP_CMD_MAIL_2_LEN);
  return SMTP_MAIL;
}

/** Prepare RCPT message */
static enum smtp_session_state
smtp_prepare_rcpt(struct smtp_session *s, u16_t *tx_buf_len)
{
  char *target = s->tx_buf;
  *tx_buf_len = SMTP_CMD_RCPT_1_LEN + SMTP_CMD_RCPT_2_LEN + s->to_len;
  LWIP_ASSERT("tx_buf overflow detected", *tx_buf_len <= SMTP_TX_BUF_LEN);
  target[*tx_buf_len] = 0;

  SMEMCPY(target, SMTP_CMD_RCPT_1, SMTP_CMD_RCPT_1_LEN);
  target += SMTP_CMD_RCPT_1_LEN;
  memcpy(target, s->to, s->to_len);
  target += s->to_len;
  SMEMCPY(target, SMTP_CMD_RCPT_2, SMTP_CMD_RCPT_2_LEN);
  return SMTP_RCPT;
}

/** Prepare header of body */
static enum smtp_session_state
smtp_prepare_header(struct smtp_session *s, u16_t *tx_buf_len)
{
  char *target = s->tx_buf;
  *tx_buf_len = SMTP_CMD_HEADER_1_LEN + SMTP_CMD_HEADER_2_LEN +
    SMTP_CMD_HEADER_3_LEN + SMTP_CMD_HEADER_4_LEN + s->from_len + s->to_len +
    s->subject_len;
  LWIP_ASSERT("tx_buf overflow detected", *tx_buf_len <= SMTP_TX_BUF_LEN);
  target[*tx_buf_len] = 0;

  SMEMCPY(target, SMTP_CMD_HEADER_1, SMTP_CMD_HEADER_1_LEN);
  target += SMTP_CMD_HEADER_1_LEN;
  memcpy(target, s->from, s->from_len);
  target += s->from_len;
  SMEMCPY(target, SMTP_CMD_HEADER_2, SMTP_CMD_HEADER_2_LEN);
  target += SMTP_CMD_HEADER_2_LEN;
  memcpy(target, s->to, s->to_len);
  target += s->to_len;
  SMEMCPY(target, SMTP_CMD_HEADER_3, SMTP_CMD_HEADER_3_LEN);
  target += SMTP_CMD_HEADER_3_LEN;
  memcpy(target, s->subject, s->subject_len);
  target += s->subject_len;
  SMEMCPY(target, SMTP_CMD_HEADER_4, SMTP_CMD_HEADER_4_LEN);

  return SMTP_BODY;
}

/** Prepare QUIT message */
static enum smtp_session_state
smtp_prepare_quit(struct smtp_session *s, u16_t *tx_buf_len)
{
  *tx_buf_len = SMTP_CMD_QUIT_LEN;
  s->tx_buf[*tx_buf_len] = 0;
  SMEMCPY(s->tx_buf, SMTP_CMD_QUIT, SMTP_CMD_QUIT_LEN);
  LWIP_ASSERT("tx_buf overflow detected", *tx_buf_len <= SMTP_TX_BUF_LEN);
  return SMTP_CLOSED;
}

/** If in state SMTP_BODY, try to send more body data */
static void
smtp_send_body(struct smtp_session *s, struct tcp_pcb *pcb)
{
  err_t err;

  if (s->state == SMTP_BODY) {
    u16_t send_len = s->body_len - s->body_sent;
    if (send_len > 0) {
      u16_t snd_buf = tcp_sndbuf(pcb);
      if (send_len > snd_buf) {
        send_len = snd_buf;
      }
      if (send_len > 0) {
        /* try to send something out */
        err = tcp_write(pcb, &s->body[s->body_sent], (u16_t)send_len, TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK) {
          s->timer = SMTP_TIMEOUT_DATABLOCK;
          s->body_sent += send_len;
          if (s->body_sent < s->body_len) {
            LWIP_DEBUGF(SMTP_DEBUG_STATE, ("smtp_send_body: %d of %d bytes written\n",
              s->body_sent, s->body_len));
          }
        }
      }
    }
    if (s->body_sent == s->body_len) {
      /* the whole body has been written, write last line */
      LWIP_DEBUGF(SMTP_DEBUG_STATE, ("smtp_send_body: body completely written (%d bytes), appending end-of-body\n",
        s->body_len));
      err = tcp_write(pcb, SMTP_CMD_BODY_FINISHED, SMTP_CMD_BODY_FINISHED_LEN, 0);
      if (err == ERR_OK) {
        s->timer = SMTP_TIMEOUT_DATATERM;
        LWIP_DEBUGF(SMTP_DEBUG_STATE, ("smtp_send_body: end-of-body written, changing state to %s\n",
          smtp_state_str[SMTP_QUIT]));
        /* last line written, change state, wait for confirmation */
        s->state = SMTP_QUIT;
      }
    }
  }
}

/** State machine-like implementation of an SMTP client.
 */
static void
smtp_process(void *arg, struct tcp_pcb *pcb, struct pbuf *p)
{
  struct smtp_session* s = arg;
  u16_t response_code = 0;
  u16_t tx_buf_len = 0;
  enum smtp_session_state next_state;

  if (arg == NULL) {
    /* already closed SMTP connection */
    if (p != NULL) {
      LWIP_DEBUGF(SMTP_DEBUG_TRACE, ("Received %d bytes after closing: %s\n",
        p->tot_len, smtp_pbuf_str(p)));
      pbuf_free(p);
    }
    return;
  }

  next_state = s->state;

  if (p != NULL) {
    /* received data */
    if (s->p == NULL) {
      s->p = p;
    } else {
      pbuf_cat(s->p, p);
    }
  } else {
    /* idle timer, close connection if timed out */
    if (s->timer == 0) {
      LWIP_DEBUGF(SMTP_DEBUG_WARN_STATE, ("smtp_process: connection timed out, closing\n"));
      smtp_close(s, pcb, SMTP_RESULT_ERR_TIMEOUT, 0, ERR_TIMEOUT);
      return;
    }
    if (s->state == SMTP_BODY) {
      smtp_send_body(s, pcb);
      return;
    }
  }
  response_code = smtp_is_response(s);
  if (response_code) {
    LWIP_DEBUGF(SMTP_DEBUG_TRACE, ("smtp_process: received response code: %d\n", response_code));
    if (smtp_is_response_finished(s) != ERR_OK) {
      LWIP_DEBUGF(SMTP_DEBUG_TRACE, ("smtp_process: partly received response code: %d\n", response_code));
      /* wait for next packet to complete the respone */
      return;
    }
  } else {
    if (s->p != NULL) {
      LWIP_DEBUGF(SMTP_DEBUG_WARN, ("smtp_process: unknown data received (%s)\n",
        smtp_pbuf_str(s->p)));
      pbuf_free(s->p);
      s->p = NULL;
    }
    return;
  }

  switch(s->state)
  {
  case(SMTP_NULL):
    /* wait for 220 */
    if (response_code == 220) {
      /* then send EHLO */
      next_state = smtp_prepare_helo(s, &tx_buf_len, pcb);
    }
    break;
  case(SMTP_HELO):
    /* wait for 250 */
    if (response_code == 250) {
#if SMTP_SUPPORT_AUTH_PLAIN || SMTP_SUPPORT_AUTH_LOGIN
      /* then send AUTH or MAIL */
      next_state = smtp_prepare_auth_or_mail(s, &tx_buf_len);
    }
    break;
  case(SMTP_AUTH_LOGIN):
  case(SMTP_AUTH_PLAIN):
    /* wait for 235 */
    if (response_code == 235) {
#endif /* SMTP_SUPPORT_AUTH_PLAIN || SMTP_SUPPORT_AUTH_LOGIN */
      /* send MAIL */
      next_state = smtp_prepare_mail(s, &tx_buf_len);
    }
    break;
#if SMTP_SUPPORT_AUTH_LOGIN
  case(SMTP_AUTH_LOGIN_UNAME):
    /* wait for 334 Username */
    if (response_code == 334) {
      if (pbuf_strstr(s->p, SMTP_RESP_LOGIN_UNAME) != 0xFFFF) {
        /* send username */
        next_state = smtp_prepare_auth_login_uname(s, &tx_buf_len);
      }
    }
    break;
  case(SMTP_AUTH_LOGIN_PASS):
    /* wait for 334 Password */
    if (response_code == 334) {
      if (pbuf_strstr(s->p, SMTP_RESP_LOGIN_PASS) != 0xFFFF) {
        /* send username */
        next_state = smtp_prepare_auth_login_pass(s, &tx_buf_len);
      }
    }
    break;
#endif /* SMTP_SUPPORT_AUTH_LOGIN */
  case(SMTP_MAIL):
    /* wait for 250 */
    if (response_code == 250) {
      /* send RCPT */
      next_state = smtp_prepare_rcpt(s, &tx_buf_len);
    }
    break;
  case(SMTP_RCPT):
    /* wait for 250 */
    if (response_code == 250) {
      /* send DATA */
      SMEMCPY(s->tx_buf, SMTP_CMD_DATA, SMTP_CMD_DATA_LEN);
      tx_buf_len = SMTP_CMD_DATA_LEN;
      next_state = SMTP_DATA;
    }
    break;
  case(SMTP_DATA):
    /* wait for 354 */
    if (response_code == 354) {
      /* send email header */
      next_state = smtp_prepare_header(s, &tx_buf_len);
    }
    break;
  case(SMTP_BODY):
    /* nothing to be done here, handled somewhere else */
    break;
  case(SMTP_QUIT):
    /* wait for 250 */
    if (response_code == 250) {
      /* send QUIT */
      next_state = smtp_prepare_quit(s, &tx_buf_len);
    }
    break;
  case(SMTP_CLOSED):
    /* nothing to do, wait for connection closed from server */
    return;
  default:
    LWIP_DEBUGF(SMTP_DEBUG_SERIOUS, ("Invalid state: %d/%s\n", (int)s->state,
      smtp_state_str[s->state]));
    break;
  }
  if (s->state == next_state) {
    LWIP_DEBUGF(SMTP_DEBUG_WARN_STATE, ("smtp_process[%s]: unexpected response_code, closing: %d (%s)\n",
      smtp_state_str[s->state], response_code, smtp_pbuf_str(s->p)));
    /* close connection */
    smtp_close(s, pcb, SMTP_RESULT_ERR_SVR_RESP, response_code, ERR_OK);
    return;
  }
  if (tx_buf_len > 0) {
    SMTP_TX_BUF_MAX(tx_buf_len);
    if (tcp_write(pcb, s->tx_buf, tx_buf_len, TCP_WRITE_FLAG_COPY) == ERR_OK) {
      LWIP_DEBUGF(SMTP_DEBUG_TRACE, ("smtp_process[%s]: received command %d (%s)\n",
        smtp_state_str[s->state], response_code, smtp_pbuf_str(s->p)));
      LWIP_DEBUGF(SMTP_DEBUG_TRACE, ("smtp_process[%s]: sent %"U16_F" bytes: \"%s\"\n",
        smtp_state_str[s->state], tx_buf_len, s->tx_buf));
      s->timer = SMTP_TIMEOUT;
      pbuf_free(s->p);
      s->p = NULL;
      LWIP_DEBUGF(SMTP_DEBUG_STATE, ("smtp_process: changing state from %s to %s\n",
        smtp_state_str[s->state], smtp_state_str[next_state]));
      s->state = next_state;
      if (next_state == SMTP_BODY) {
        /* try to stream-send body data right now */
        smtp_send_body(s, pcb);
      } else if (next_state == SMTP_CLOSED) {
        /* sent out all data, delete structure */
        tcp_arg(pcb, NULL);
        smtp_free(s, SMTP_RESULT_OK, 0, ERR_OK);
      }
    }
  }
}
#endif /* LWIP_TCP */
