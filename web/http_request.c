#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stddef.h>

#include "http_request.h"
#include "http_parser.h"
#include "mevent/config.h"
#include "config.h"
#include "mevent/ring_buffer.h"
#include "dict.h"
#include "str.h"
#include "mevent/connection.h"
#include "http.h"

#define OK    (0)
#define AGAIN (1)
#define ERROR (-1)


extern config server_config;




static int request_handle_request_line(request *r);
static int request_handle_headers(request *r);
static int request_handle_body(request *r);



static int response_handle_send_buffer(request *r);
static int response_handle_send_file( request *r);
static int response_assemble_buffer( request *r);
static int response_assemble_err_buffer( request *r, int status_code);

typedef int (*header_handle_method)(request *, size_t);

/* handlers for specific http headers */
static int request_handle_hd_base(request *r, size_t offset);
static int request_handle_hd_connection(request *r, size_t offset);
static int request_handle_hd_content_length(request *r, size_t offset);
static int request_handle_hd_transfer_encoding(request *r, size_t offset);

typedef struct {
  ssstr hd;
  header_handle_method func;
  size_t offset;
} header_func;

#define XX(hd, hd_mn, func)  \
  { SSSTR(hd), func, offsetof(request_headers_t, hd_mn) }

static header_func hf_list[] = {
    XX("accept", accept, request_handle_hd_base),
    XX("accept-charset", accept_charset, request_handle_hd_base),
    XX("accept-encoding", accept_encoding, request_handle_hd_base),
    XX("accept-language", accept_language, request_handle_hd_base),
    XX("cache-control", cache_control, request_handle_hd_base),
    XX("content-length", content_length, request_handle_hd_content_length),
    XX("connection", connection, request_handle_hd_connection),
    XX("cookie", cookie, request_handle_hd_base),
    XX("date", date, request_handle_hd_base),
    XX("host", host, request_handle_hd_base),
    XX("if-modified-since", if_modified_since, request_handle_hd_base),
    XX("if-unmodified-since", if_unmodified_since, request_handle_hd_base),
    XX("max-forwards", max_forwards, request_handle_hd_base),
    XX("range", range, request_handle_hd_base),
    XX("referer", referer, request_handle_hd_base),
    XX("transfer-encoding", transfer_encoding,
       request_handle_hd_transfer_encoding),
    XX("user-agent", user_agent, request_handle_hd_base),
};
#undef XX

dict_t header_handler_dict;

void header_handler_dict_init() {
  dict_init(&header_handler_dict);
  size_t nsize = sizeof(hf_list) / sizeof(hf_list[0]);
  int i;
  for (i = 0; i < nsize; i++) {
    dict_put(&header_handler_dict, &hf_list[i].hd, (void *)&hf_list[i]);
  }
}

void header_handler_dict_free() { dict_free(&header_handler_dict); }



int http_request(request* req)
{
    if (!req)  {
        return -1;
    }
    int status = OK;
    do {
        status = req->req_handler(req);
    } while(req->req_handler != NULL && status == OK);
}



void http_request_handle_init(connection* conn)
{
    request* req = (request*)mu_malloc(sizeof(request));
    memset(req, 0, sizeof(request));
    conn->handler = req;
    req->conn = conn;

    parse_archive_init(&req->par);
    req->resource_fd = -1;
    req->status_code = 200;

    req->req_handler = request_handle_request_line;
}


static int request_handle_request_line(request *r) {
  int status;
  int msg_len = 0;
  char* msg = ring_buffer_get_msg(r->conn->ring_buffer_read, &msg_len);
  if (!r->par.next_parse_pos)  {
      r->par.next_parse_pos = msg;
  }
  
  status = parse_request_line(msg, msg_len, &r->par);
  if (status == AGAIN) // not a complete request line
    return AGAIN;
  if (status != OK) { // INVALID_REQUEST
    return response_assemble_err_buffer(r, 400);
  }
  // status = OK now
  parse_archive *archive = &(r->par);

  if (archive->version.http_major > 1 || archive->version.http_minor > 1) {
    // send 505 error response to client
    return response_assemble_err_buffer(r, 505);
  }
  archive->keep_alive = (archive->version.http_major == 1 && archive->version.http_minor == 1);

  // make `relative_path` a c-style string, really ugly....
  char *p = archive->url.abs_path.str;
  while (*p != ' ' && *p != '?')
    p++;
  *p = '\0';

  /* check abs_path */
  const char *relative_path = NULL;
  relative_path = archive->url.abs_path.len == 1 && archive->url.abs_path.str[0] == '/'
                      ? "./"
                      : archive->url.abs_path.str + 1;

  int fd = openat(server_config.rootdir_fd, relative_path, O_RDONLY);
  if (fd == ERROR) {
    // send 404 error response to client
    return response_assemble_err_buffer(r, 404);
  }
  struct stat st;
  fstat(fd, &st);

  if (S_ISDIR(st.st_mode)) { // substitute dir to index.html
    // fd is a dir fildes
    int html_fd = openat(fd, "index.html", O_RDONLY);
    close(fd);
    if (fd == ERROR) {
      // send 404 error response to client
      return response_assemble_err_buffer(r, 404);
    }
    fd = html_fd;
    fstat(fd, &st);
    ssstr_set(&archive->url.mime_extension, "html");
  }
  r->resource_fd = fd;
  r->resource_size = st.st_size;
  r->req_handler = request_handle_headers;
  return OK;
}

static int request_handle_headers(request *r) {
  int status;
  parse_archive *archive = &r->par;

  int msg_len = 0;
  char* msg = ring_buffer_get_msg(r->conn->ring_buffer_read, &msg_len);

  while (true) {
    status = parse_header_line(msg, msg_len, archive);
    switch (status) {
    /* not a complete header */
    case AGAIN:
      return AGAIN;
    /* header invalid */
    case INVALID_REQUEST:
      // send error response to client
      return response_assemble_err_buffer(r, 400);
    /* all headers completed */
    case CRLF_LINE:
      goto header_done;
    /* a header completed */
    case OK:
      ssstr_tolower(&r->par.header[0]);

      // handle header individually
      header_func *hf = dict_get(&header_handler_dict, &r->par.header[0], NULL);
      if (hf == NULL)
        break;
      header_handle_method func = hf->func;
      size_t offset = hf->offset;
      if (func != NULL) {
        status = func(r, offset);
        if (status != OK)
          return OK;
      }
      break;
    }
  }
header_done:;
  r->req_handler = request_handle_body;
  return OK;
}

static int request_handle_body(request *r) {
  int status;
  parse_archive *archive = &r->par;
  int msg_len = 0;
  char* msg = ring_buffer_get_msg(r->conn->ring_buffer_read, &msg_len);
  switch (r->par.transfer_encoding) {
  case TE_IDENTITY:
    status = parse_header_body_identity(msg, msg_len, archive);
    break;
  default:
    status = ERROR;
    break;
  }

  switch (status) {
  case AGAIN:
    return AGAIN;
  case OK:
    //connection_disable_in(epoll_fd, r->c);
    //connection_enable_out(epoll_fd, r->c);
    //r->req_handler = NULL; // body parse done !!! no more handlers
    //response_assemble_buffer(r);
    return OK;
  default:
    // send error response to client
    //return response_assemble_err_buffer(r, 501);
    return OK;
  }
  return OK;
}

/* save header value into the proper position of parse_archive.req_headers */
int request_handle_hd_base(request *r, size_t offset) {
  parse_archive *archive = &r->par;
  ssstr *header = (ssstr *)(((char *)(&archive->req_headers)) + offset);
  *header = archive->header[1];
  return OK;
}

int request_handle_hd_connection(request *r, size_t offset) {
  request_handle_hd_base(r, offset);
  ssstr *connection = &(r->par.req_headers.connection);
  if (ssstr_caseequal(connection, "keep-alive")) {
    r->par.keep_alive = true;
  } else if (ssstr_caseequal(connection, "close")) {
    r->par.keep_alive = false;
  } else {
    // send error response to client
    return response_assemble_err_buffer(r, 400);
  }
  return OK;
}

int request_handle_hd_content_length(request *r, size_t offset) {
  request_handle_hd_base(r, offset);
  ssstr *content_length = &(r->par.req_headers.content_length);
  int len = atoi(content_length->str);
  if (len <= 0) {
    // send error response to client
    return response_assemble_err_buffer(r, 400);
  }
  r->par.content_length = len;
  return OK;
}

// https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Transfer-Encoding
// https://imququ.com/post/content-encoding-header-in-http.html
int request_handle_hd_transfer_encoding(request *r, size_t offset) {
  request_handle_hd_base(r, offset);
  ssstr *transfer_encoding = &(r->par.req_headers.transfer_encoding);
  if (ssstr_caseequal(transfer_encoding, "chunked")) {
    // may implement, send error response to client
    r->par.transfer_encoding = TE_CHUNKED;
    return response_assemble_err_buffer(r, 501);
  } else if (ssstr_caseequal(transfer_encoding, "compress")) {
    // send error response to client
    r->par.transfer_encoding = TE_COMPRESS;
    return response_assemble_err_buffer(r, 501);
  } else if (ssstr_caseequal(transfer_encoding, "deflate")) {
    // send error response to client
    r->par.transfer_encoding = TE_DEFLATE;
    return response_assemble_err_buffer(r, 501);
  } else if (ssstr_caseequal(transfer_encoding, "gzip")) {
    // send error response to client
    r->par.transfer_encoding = TE_GZIP;
    return response_assemble_err_buffer(r, 501);
  } else if (ssstr_caseequal(transfer_encoding, "identity")) {
    r->par.transfer_encoding = TE_IDENTITY;
    return response_assemble_err_buffer(r, 501);
  } else {
    // send error response to client
    return response_assemble_err_buffer(r, 400);
  }
  return OK;
}

/**
 * Return:
 * OK: all data sent
 * AGAIN: haven't sent all data
 * ERROR: error
 */
static int response_send(request *r) {
//   int len = 0;
//   buffer_t *b = r->ob;
//   char *buf_beg;
//   while (true) {
//     buf_beg = b->buf + r->par.buffer_sent;
//     len = send(r->c->fd, buf_beg, buffer_end(b) - buf_beg, 0);
//     if (len == 0) {
//       buffer_clear(b);
//       r->par.buffer_sent = 0;
//       return OK;
//     } else if (len < 0) {
//       if (errno == EAGAIN)
//         return AGAIN;
//       lotos_log(LOG_ERR, "send: %s", strerror(errno));
//       return ERROR;
//     }
//     r->par.buffer_sent += len;
//   }
  return OK;
}

int response_handle( connection *c) {
//   request *r = &c->req;
   int status;
//   do {
//     status = r->res_handler(r);
//   } while (status == OK && r->par.response_done != true);
//   if (r->par.response_done) { // response done
//     if (r->par.keep_alive) {
//       request_reset(r);
//       connection_disable_out(epoll_fd, c);
//       connection_enable_in(epoll_fd, c);
//     } else
//       return ERROR; // make connection expired
//   }
  return status;
}

int response_handle_send_buffer( request *r) {
//   int status;
//   status = response_send(r);
//   if (status != OK) {
//     return status;
//   } else {
//     if (r->resource_fd != -1) {
//       r->res_handler = response_handle_send_file;
//       return OK;
//     }
//     r->par.response_done = true;
//     connection_disable_out(epoll_fd, r->c);
//     return OK;
//   }
  return OK;
}

int response_handle_send_file( request *r) {
//   int len;
//   connection_t *c = r->c;
//   while (true) {
//     // zero copy, make it faster
//     len = sendfile(c->fd, r->resource_fd, NULL, r->resource_size);
//     if (len == 0) {
//       r->par.response_done = true;
//       close(r->resource_fd);
//       return OK;
//     } else if (len < 0) {
//       if (errno == EAGAIN) {
//         return AGAIN;
//       }
//       lotos_log(LOG_ERR, "sendfile: %s", strerror(errno));
//       return ERROR;
//     }
//   }
  return OK;
}

int response_assemble_buffer( request *r) {
//   response_append_status_line(r);
//   response_append_date(r);
//   response_append_server(r);
//   response_append_content_type(r);
//   response_append_content_length(r);
//   response_append_connection(r);
//   response_append_crlf(r);
  return OK;
}

int response_assemble_err_buffer( request *r, int status_code) {
//   r->req_handler = NULL;
//   r->par.err_req = true;
//   r->status_code = status_code;

//   response_append_status_line(r);
//   response_append_date(r);
//   response_append_server(r);
//   response_append_content_type(r);
//   response_append_content_length(r);
//   r->par.keep_alive = false;
//   response_append_connection(r);
//   response_append_crlf(r);

//   // add err page html
//   r->ob = buffer_cat_cstr(r->ob, err_page_render_buf());

//   connection_disable_in(epoll_fd, r->c);
//   connection_enable_out(epoll_fd, r->c);
//   r->par.response_done = true;
  return OK;
}