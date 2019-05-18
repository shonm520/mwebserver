
#include <assert.h>
#include <string.h>

#include "http_parser.h"

#define AGAIN (1)
#define OK    (0)
#define ERROR (-1)

#define STR2_EQ(p, q) ((p)[0] == (q)[0] && (p)[1] == (q)[1])
#define STR3_EQ(p, q) (STR2_EQ(p, q) && (p)[2] == (q)[2])
#define STR4_EQ(p, q) (STR2_EQ(p, q) && STR2_EQ(p + 2, q + 2))
#define STR5_EQ(p, q) (STR2_EQ(p, q) && STR3_EQ(p + 2, q + 2))
#define STR6_EQ(p, q) (STR3_EQ(p, q) && STR3_EQ(p + 3, q + 3))
#define STR7_EQ(p, q) (STR3_EQ(p, q) && STR4_EQ(p + 3, q + 3))

#define HEADER_SET(header, str_beg, str_end)                                   \
  do {                                                                         \
    assert(str_beg <= str_end);                                                \
    (header)->str = str_beg;                                                   \
    (header)->len = (str_end) - (str_beg);                                     \
  } while (0)

static int parse_method(char *begin, char *end);
static int parse_url(char *begin, char *end, parse_archive *ar);

/* parse request line */
/**
 * @return
 * OK: request line OK
 * AGAIN: parse to the end of buffer, but no complete request line
 * INVALID_REQUEST request not valid
 * Method Request-URI HTTP-Version CRLF  
 * eg:GET /form.html HTTP/1.1 (CRLF)
 */
int parse_request_line(char* msg, int* len, parse_archive *ar) 
{
  assert(len);
  char *p;
  for (p = ar->next_parse_pos; p < (msg + *len); p++) {
    char ch = *p;
    switch (ar->state) {
    case S_RL_BEGIN:
      switch (ch) {
      case 'a' ... 'z':
      case 'A' ... 'Z':
        ar->method_begin = p;     //记录method开始的地方
        ar->state = S_RL_METHOD;
        break;
      default:
        return INVALID_REQUEST;
      } // end S_RL_BEGIN

    case S_RL_METHOD:
      switch (ch) {
      case 'a' ... 'z':
      case 'A' ... 'Z':
        break;
      case ' ': {                //直到空格
        ar->method = parse_method(ar->method_begin, p);
        if (ar->method == HTTP_INVALID)
          return INVALID_REQUEST;
        ar->state = S_RL_SP_BEFORE_URL;
        break;
      default:
        return INVALID_REQUEST;
      }
      } // end S_RL_METHOD
      break;
    case S_RL_SP_BEFORE_URL:
      switch (ch) {
      case ' ':
      case '\t': /* ease parser, '\t' is also considered valid */
        break;
      case '\r':
      case '\n':
        return INVALID_REQUEST;
      default:
        ar->state = S_RL_URL;
        ar->url_begin = p;
      }
      break;

    case S_RL_URL:
      switch (ch) {
      case ' ':
      case '\t':
        // assume url part has been received completely
        ar->state = S_RL_SP_BEFORE_VERSION;
        int url_status = parse_url(ar->url_begin, p, ar);
        if (url_status)
          return url_status;
        break;
      case '\r':
      case '\n':
        return INVALID_REQUEST;
      default:
        break;
      } // end S_RL_URL
      break;
    case S_RL_SP_BEFORE_VERSION:
      switch (ch) {
      case ' ':
      case '\t':
        break;
      case 'H':
      case 'h':
        ar->state = S_RL_VERSION_H;
        break;
      default:
        return INVALID_REQUEST;
      } // end S_RL_SP_BEFORE_RL_VERSION
      break;
    case S_RL_VERSION_H:
      switch (ch) {
      case 'T':
      case 't':
        ar->state = S_RL_VERSION_HT;
        break;
      default:
        return INVALID_REQUEST;
      } // end S_RL_VERSION_H
      break;
    case S_RL_VERSION_HT:
      switch (ch) {
      case 'T':
      case 't':
        ar->state = S_RL_VERSION_HTT;
        break;
      default:
        return INVALID_REQUEST;
      } // end S_RL_VERSION_HT
      break;
    case S_RL_VERSION_HTT:
      switch (ch) {
      case 'P':
      case 'p':
        ar->state = S_RL_VERSION_HTTP;
        break;
      default:
        return INVALID_REQUEST;
      } // end S_RL_VERSION_HTT
      break;
    case S_RL_VERSION_HTTP:
      switch (ch) {
      case '/':
        ar->state = S_RL_VERSION_HTTP_SLASH;
        break;
      default:
        return INVALID_REQUEST;
      } // end S_RL_VERSION_HTTP
      break;
    case S_RL_VERSION_HTTP_SLASH:
      switch (ch) {
      case '0' ... '9':
        ar->version.http_major = ar->version.http_major * 10 + ch - '0';
        ar->state = S_RL_VERSION_MAJOR;
        break;
      default:
        return INVALID_REQUEST;
      } // end S_RL_VERSION_HTTP_SLASH
      break;
    case S_RL_VERSION_MAJOR:
      switch (ch) {
      case '0' ... '9':
        ar->version.http_major = ar->version.http_major * 10 + ch - '0';
        if (ar->version.http_major > 1)
          return INVALID_REQUEST;
        break;
      case '.':
        ar->state = S_RL_VERSION_DOT;
        break;
      default:
        return INVALID_REQUEST;
      } // end S_RL_VERSION_MAJOR
      break;
    case S_RL_VERSION_DOT:
      switch (ch) {
      case '0' ... '9':
        ar->version.http_minor = ar->version.http_minor * 10 + ch - '0';
        ar->state = S_RL_VERSION_MINOR;
        break;
      default:
        return INVALID_REQUEST;
      } // end S_RL_VERSION_DOT
      break;
    case S_RL_VERSION_MINOR:
      switch (ch) {
      case '0' ... '9':
        ar->version.http_minor = ar->version.http_minor * 10 + ch - '0';
        if (ar->version.http_minor > 1)
          return INVALID_REQUEST;
        break;
      case '\r':
        ar->state = S_RL_CR_AFTER_VERSION;
        break;
      default:
        return INVALID_REQUEST;
      } // end S_RL_VERSION_MINOR
      break;
    case S_RL_CR_AFTER_VERSION:
      switch (ch) {
      case '\n':
        ar->state = S_RL_LF_AFTER_VERSION;
        /* parse request line done*/
        goto done;
      default:
        return INVALID_REQUEST;
      } // end S_RL_CR_AFTER_VERSION
      break;
    } // end switch(state)
  }   // end for
  ar->next_parse_pos = *len;
  return AGAIN;
done:;
  ar->next_parse_pos = p + 1;
  *len = p + 1 - msg;
  ar->state = S_HD_BEGIN;
  return OK;
}

/* parse header line */
/**
 * @return
 *  OK: one header line has been parsed
 *  AGAIN: parse to the end of buffer, but no complete header
 *  INVALID_REQUEST request not valid
 *  CRLF_LINE: `\r\n`, which means all headers have been parsed
 *
 */
int parse_header_line(char* msg, int *len, parse_archive *ar) 
{
  char ch, *p;
  assert(len);
  for (p = ar->next_parse_pos; p < (msg + *len); p++) {
    ch = *p;
    switch (ar->state) {
    case S_HD_BEGIN:
      switch (ch) {
      case 'A' ... 'Z':
      case 'a' ... 'z':
      case '0' ... '9':
      case '-':
        ar->state = S_HD_NAME;
        ar->header_line_begin = p;
        ar->isCRLF_LINE = false;
        break;
      case '\r':
        ar->state = S_HD_CR_AFTER_VAL;
        ar->isCRLF_LINE = true;
        break;
      case ' ':
      case '\t':
        break;
      default:
        return INVALID_REQUEST;
      }
      break;

    case S_HD_NAME:
      switch (ch) {
      case 'A' ... 'Z':
      case 'a' ... 'z':
      case '0' ... '9':
      case '-':
        break;
      case ':':
        ar->state = S_HD_COLON;
        ar->header_colon_pos = p;
        break;
      default:
        return INVALID_REQUEST;
      }
      break;

    case S_HD_COLON:
      switch (ch) {
      case ' ':
      case '\t':
        ar->state = S_HD_SP_BEFORE_VAL;
        break;
      case '\r':
      case '\n':
        return INVALID_REQUEST;
      default:
        ar->state = S_HD_VAL;
        ar->header_val_begin = p;
        break;
      }
      break;

    case S_HD_SP_BEFORE_VAL:
      switch (ch) {
      case ' ':
      case '\t':
        break;
      case '\r':
      case '\n':
        return INVALID_REQUEST;
      default:
        ar->state = S_HD_VAL;
        ar->header_val_begin = p;
        break;
      }
      break;

    case S_HD_VAL:
      switch (ch) {
      case '\r':
        ar->header_val_end = p;
        ar->state = S_HD_CR_AFTER_VAL;
        break;
      case '\n':
        ar->state = S_HD_LF_AFTER_VAL;
        break;
      default:
        break;
      }
      break;

    case S_HD_CR_AFTER_VAL:
      switch (ch) {
      case '\n':
        ar->state = S_HD_LF_AFTER_VAL;
        goto done;
      default:
        return INVALID_REQUEST;
      }
      break;
    } // end switch state
  }   // end for
  ar->next_parse_pos = *len;
  return AGAIN;
done:;
  ar->next_parse_pos = p + 1;
  *len = p + 1 - msg;
  ar->state = S_HD_BEGIN;
  ar->num_headers++;

  /* put header name and val into header[2] */
  HEADER_SET(&ar->header[0], ar->header_line_begin, ar->header_colon_pos);
  HEADER_SET(&ar->header[1], ar->header_val_begin, ar->header_val_end);
  return ar->isCRLF_LINE ? CRLF_LINE : OK;
}

static int parse_method(char *begin, char *end) 
{
  int len = end - begin;
  switch (len) {
  case 3:
    if (STR3_EQ(begin, "GET")) {
      return HTTP_GET;
    } else if (STR3_EQ(begin, "PUT")) {
      return HTTP_PUT;
    } else {
      return HTTP_INVALID;
    }
    break;
  case 4:
    if (STR4_EQ(begin, "POST")) {
      return HTTP_POST;
    } else if (STR4_EQ(begin, "HEAD")) {
      return HTTP_HEAD;
    } else {
      return HTTP_INVALID;
    }
    break;
  case 6:
    if (STR6_EQ(begin, "DELETE")) {
      return HTTP_DELETE;
    } else {
      return HTTP_INVALID;
    }
    break;
  default:
    return HTTP_INVALID;
  }
  return HTTP_INVALID;
}



#define Move_Str(item, src, len_) \
  do { \
    (item).str = src; \
    (item).len = len_; \
  } while(0)

/**
 * Some Samples:
 *
 * /abc/def/
 * /unp.pdf
 * /unp.pdf/  `dir`
 * /abc.def/set?name=chen&val=newbie
 * /video/life.of.pi.BlueRay.rmvb
 * /video/life.of.pi.BlueRay  `dir`
 */
/* simple parse url */
static int parse_url(char *begin, char *end, parse_archive *ar)
 {
  int len = end - begin;
  assert(len >= 0);
  Move_Str(ar->request_url_string, begin, len);
  int curr_state = S_URL_BEGIN;

  char ch;
  char *p = begin;
  for (; p != end + 1; p++) {
    ch = *p;
    switch (curr_state) {
    case S_URL_BEGIN:
      switch (ch) {
      case '/':
        curr_state = S_URL_ABS_PATH;
        break;
      default:
        return ERROR;
      }
      break;

    case S_URL_ABS_PATH:
      switch (ch) {
      case ' ':
        Move_Str(ar->url.abs_path, begin, p - begin);
        Move_Str(ar->url.query_string, p, 0);
        curr_state = S_URL_END;
        break;
      case '?':
        Move_Str(ar->url.abs_path, begin, p - begin);
        begin = p + 1;
        curr_state = S_URL_QUERY;
        break;
      default:
        break;
      }
      break;

    case S_URL_QUERY:
      switch (ch) {
      case ' ':
        Move_Str(ar->url.query_string, begin, p - begin);
        curr_state = S_URL_END;
      default:
        break;
      }
      break;

    case S_URL_END:
      goto parse_extension;
    } // end switch(curr_state)
  }   // end for

parse_extension:;
  // directory extension will be corrected in `request_handle_request_line`
  char *abs_path_end = ar->url.abs_path.str + ar->url.abs_path.len;

  for (p = abs_path_end; p != ar->url.abs_path.str; p--) {
    if (*p == '.')  {
      Move_Str(ar->url.mime_extension, p + 1, abs_path_end - p - 1);
      break;
    }  else if (*p == '/')
      break;
  }

  return OK;
}

int parse_header_body_identity(char* msg, int *len, parse_archive *ar) 
{
  if (ar->content_length <= 0)
    return OK;
  // not that complicated, using `next_parse_pos` to indicate where to parse
  size_t received = *len;
  ar->body_received += received;

  ar->next_parse_pos = msg + *len;

  if (ar->body_received >= ar->content_length)  { // full data recv
    return OK;
  }
  return AGAIN; // will conitinue to recv until full data recv or conn timeout
}
