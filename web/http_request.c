#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>

#include "http_request.h"
#include "http_parser.h"
#include "mevent/config.h"
#include "config.h"
#include "mevent/ring_buffer.h"
#include "dict.h"
#include "str.h"
#include "mevent/connection.h"


#define OK    (0)
#define AGAIN (1)
#define ERROR (-1)


extern config server_config;




static int request_handle_request_line(request *r);
static int request_handle_headers(request *r);
static int request_handle_body(request *r);



static int response_handle_send_line_and_header(request *r);
static int response_handle_send_file( request *r);
static int response_assemble_err_buffer( request *r, int status_code);


typedef int (*header_handle_method)(request *, void*);

typedef struct header_func_t{
  ssstr hd;
  header_handle_method func;
  size_t offset;
} header_func;



/* handlers for specific http headers */
static int request_handle_hd_base(request *r, void*);
static int request_handle_hd_connection(request *r, void*);
static int request_handle_hd_content_length(request *r, void*);
static int request_handle_hd_transfer_encoding(request *r, void*);

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
    XX("transfer-encoding", transfer_encoding, request_handle_hd_transfer_encoding),
    XX("user-agent", user_agent, request_handle_hd_base),
};
#undef XX

dict_t header_handler_dict;

void header_handler_dict_init()
 {
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
    int len = ring_buffer_readable_bytes(req->conn->ring_buffer_read);
    do  {
        status = req->req_handler(req);
    }  while(req->req_handler != NULL && status == OK);

    ring_buffer_release_bytes(req->conn->ring_buffer_read, len);

    if (status == OK)  {
        response_handle(req);
    }
    else  {
        response_assemble_err_buffer(req, status);
    }

    if (!req->par.keep_alive)  {           //short connection should active close connection after a request
        connection_active_close(req->conn);
    }

    http_request_handle_reset(req);        //reset connect
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
    req->res_handler = response_handle_send_line_and_header;
}


void http_request_handle_reset(request* req)
{
    parse_archive_init(&req->par);
    if (req->resource_fd > 0)  {
        close(req->resource_fd);
    }
    req->resource_fd = -1;
    req->status_code = 200;

    req->req_handler = request_handle_request_line;
    req->res_handler = response_handle_send_line_and_header;
}


static int request_handle_request_line(request *r)     //parse request line
{       
    int msg_len = 0;
    char* msg = ring_buffer_get_msg(r->conn->ring_buffer_read, &msg_len);
    if (!r->par.next_parse_pos)  {
        r->par.next_parse_pos = msg;
    }
    
    int status = parse_request_line(msg, &msg_len, &r->par);
    if (status != OK)  {
        if (status == AGAIN)  {
            return AGAIN;
        }
        else   {
            return 400;
        }
    }  
    
    // status = OK now
    parse_archive *archive = &(r->par);

    if (archive->version.http_major > 1 || archive->version.http_minor > 1) {
        return 500;
    }
    archive->keep_alive = (archive->version.http_major == 1 && archive->version.http_minor == 1);

    // make `relative_path` a c-style string, really ugly....
    char *p = archive->url.abs_path.str;
    while (*p && *p != ' ' && *p != '?') {
        p++;
    }
    *p = '\0';

    /* check abs_path */
    const char *relative_path = NULL;
    relative_path = archive->url.abs_path.len == 1 && archive->url.abs_path.str[0] == '/' ? "./" : archive->url.abs_path.str + 1;

    int fd = openat(server_config.rootdir_fd, relative_path, O_RDONLY);
    if (fd == ERROR)  {
        printf("file %s not found\n", relative_path);
        perror("file not found");
        return 404;
    }
    struct stat st;
    fstat(fd, &st);

    if (S_ISDIR(st.st_mode)) {   // substitute dir to index.html
        int html_fd = openat(fd, "index.html", O_RDONLY);
        close(fd);
        if (html_fd == ERROR) {
            return 404;
        }
        fstat(html_fd, &st);
        ssstr_set(&archive->url.mime_extension, "html");
        fd = html_fd;
    }
    r->resource_fd = fd;       //shoudl not close(fd) because used in sendfile
    r->resource_size = st.st_size;
    r->req_handler = request_handle_headers;
    return OK;
}

static int request_handle_headers(request *r)     //parse request header
{    
    int status;
    parse_archive *archive = &r->par;

    int msg_len = 0;
    int msg_left = 0;
    char* msg = ring_buffer_get_msg(r->conn->ring_buffer_read, &msg_len);
    msg_left = msg_len;
        
    while (true) {
        status = parse_header_line(msg, &msg_len, archive);      //msg_len is in and out 
        switch (status)  {
        case AGAIN:                 // not a complete header 
            debug_msg("parse request header line error: not completed\n");
            return AGAIN;
        case INVALID_REQUEST:       // header invalid
            debug_msg("parse request header line error: invalide request\n");
            return 400;
        case CRLF_LINE:             // all headers completed 
            goto header_done;
        case OK:                    // a header completed 
            msg += msg_len;
            msg_left -= msg_len;
            msg_len = msg_left;
            ssstr_tolower(&r->par.header[0]);

            // handle header individually
            header_func *hf = dict_get(&header_handler_dict, &r->par.header[0], NULL);
            if (hf == NULL)
                break;
            header_handle_method func = hf->func;
            if (func != NULL) {
                status = func(r, hf);
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

static int request_handle_body(request *r)   //parse request body
{   
    int status;
    parse_archive *archive = &r->par;
    int msg_len = 0;
    char* msg = ring_buffer_get_msg(r->conn->ring_buffer_read, &msg_len);
    switch (r->par.transfer_encoding) {
    case TE_IDENTITY:
        status = parse_header_body_identity(msg, &msg_len, archive);
        break;
    default:
        status = ERROR;
        break;
    }

    switch (status)   {
    case AGAIN:
        return AGAIN;
    case OK:
        r->req_handler = NULL; // body parse done !!! no more handlers
        return OK;
    default:
        return ERROR;
    }
    return OK;
}

/* save header value into the proper position of parse_archive.req_headers */
int request_handle_hd_base(request *r, void* hf)
 {
    parse_archive *archive = &r->par;
    header_func* hf_ = (header_func*)hf;
    size_t offset = hf_->offset;
    ssstr *item = (ssstr *)(((char *)(&archive->req_headers)) + offset);
    *item = archive->header[1];
    return OK;
}

int request_handle_hd_connection(request *r, void* hf) 
{
    request_handle_hd_base(r, hf);
    ssstr *connection = &(r->par.req_headers.connection);
    if (ssstr_caseequal(connection, "keep-alive")) {
        r->par.keep_alive = true;
    }  else if (ssstr_caseequal(connection, "close")) {
        r->par.keep_alive = false;
    }  else {
        return 400;
    }
    return OK;
}

int request_handle_hd_content_length(request *r, void* hf) 
{
    request_handle_hd_base(r, hf);
    ssstr *content_length = &(r->par.req_headers.content_length);
    int len = atoi(content_length->str);
    if (len <= 0) {
        return 400;
    }
    r->par.content_length = len;
    return OK;
}

// https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Transfer-Encoding
// https://imququ.com/post/content-encoding-header-in-http.html
int request_handle_hd_transfer_encoding(request *r, void* hf) 
{
    request_handle_hd_base(r, hf);
    ssstr *transfer_encoding = &(r->par.req_headers.transfer_encoding);
    if (ssstr_caseequal(transfer_encoding, "chunked")  ||
        ssstr_caseequal(transfer_encoding, "compress") ||
        ssstr_caseequal(transfer_encoding, "deflate")  ||
        ssstr_caseequal(transfer_encoding, "gzip")     ||
        ssstr_caseequal(transfer_encoding, "identity"))  {
            return 501;   //Not Implemented
        }
    else  {
        return 400;       //Request Time-out
    }
    return OK;
}



int response_handle( request* r) 
{
    int status;
    do  {
        status = r->res_handler(r);
    }  while(status == OK && !r->par.response_done);

    //todo close connection ?
    return status;
}

int response_handle_send_line_and_header(request *r) 
{
    response_append_status_line(r);
    response_append_date(r);
    response_append_server(r);
    response_append_content_type(r);
    response_append_content_length(r);
    response_append_connection(r);
    response_append_timeout(r);
    response_append_crlf(r);

    int ret = connection_send_buffer(r->conn);
    if (ret == 0)  {
        if (r->resource_fd != -1) {
            r->res_handler = response_handle_send_file;
        }
        else  {
            r->par.response_done = true;
        }
        return OK;
    }
    else if (ret == 1) {
        if (r->resource_fd != -1) {
            //todo   read file and send file in buff
        } 
        r->par.response_done = true;
        return OK;
    }
    return 500;
}

int response_handle_send_file( request *r) 
{
    int len = sendfile(r->conn->connfd, r->resource_fd, NULL, r->resource_size);
    if (len == 0 || r->resource_size == len)  {
        r->par.response_done = true;
        return OK;
    }
    return 500;
}


int response_assemble_err_buffer(request *r, int status_code) {
    r->req_handler = NULL;
    r->par.err_req = true;
    r->status_code = status_code;

    int resource_size = 0;
    int resource_fd = openat(server_config.rootdir_fd, "error.html", O_RDONLY);
    if (resource_fd > 0)  {
        struct stat st;
        fstat(resource_fd, &st);
        resource_size = st.st_size;
    }

    response_append_status_line(r);
    response_append_date(r);
    response_append_server(r);
    response_append_content_type(r);
    response_append_content_length(r);
    r->par.keep_alive = false;
    response_append_connection(r);
    response_append_crlf(r);

    if (resource_fd > 0 && resource_size > 0)  {
        sendfile(r->conn->connfd, resource_fd, NULL, resource_size);
        close(resource_fd);
    }

    r->par.response_done = true;
    return OK;
}