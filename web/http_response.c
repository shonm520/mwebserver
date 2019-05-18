#include <time.h>
#include <stdio.h>
#include "mevent/ring_buffer.h"
#include "mevent/connection.h"
#include "http_response.h"
#include "http_request.h"
#include "str.h"
#include "dict.h"
#include "config.h"


#define CRLF "\r\n"

extern config server_config;

static const char *Status_Table[512];

static ssstr Mime_List[][2] = {
    {SSSTR("word"), SSSTR("application/msword")},
    {SSSTR("pdf"), SSSTR("application/pdf")},
    {SSSTR("zip"), SSSTR("application/zip")},
    {SSSTR("js"), SSSTR("application/javascript")},
    {SSSTR("gif"), SSSTR("image/gif")},
    {SSSTR("jpeg"), SSSTR("image/jpeg")},
    {SSSTR("jpg"), SSSTR("image/jpeg")},
    {SSSTR("png"), SSSTR("image/png")},
    {SSSTR("css"), SSSTR("text/css")},
    {SSSTR("html"), SSSTR("text/html")},
    {SSSTR("htm"), SSSTR("text/html")},
    {SSSTR("txt"), SSSTR("text/plain")},
    {SSSTR("xml"), SSSTR("text/xml")},
    {SSSTR("svg"), SSSTR("image/svg+xml")},
    {SSSTR("mp4"), SSSTR("video/mp4")},
};

static dict_t Mime_Dict;

void mime_dict_init() 
{
  size_t nsize = sizeof(Mime_List) / sizeof(Mime_List[0]);
  int i;
  dict_init(&Mime_Dict);
  for (i = 0; i < nsize; i++) {
    dict_put(&Mime_Dict, &Mime_List[i][0], &Mime_List[i][1]);
  }
}

void mime_dict_free() { dict_free(&Mime_Dict); }

void status_table_init() {
  memset(Status_Table, 0, sizeof(Status_Table));
#define XX(num, name, string) Status_Table[num] = #num " " #string;
  HTTP_STATUS_MAP(XX);
#undef XX
}

void response_append_status_line(request *r) 
{
    ring_buffer* buf = r->conn->ring_buffer_write;

    char temp[64] = {0};
    if (r->par.version.http_major == 1)  {
        strcpy(temp, "HTTP/1.1 ");
    }
    else  {
        strcpy(temp, "HTTP/1.0 ");
    }
    const char* str_status = Status_Table[r->status_code];
    if (str_status != NULL)  {
        strcat(temp, str_status);
    }
    strcat(temp, CRLF);

    ring_buffer_push_data(buf, temp, strlen(temp));
}



void response_append_date(request *r)
 {
    ring_buffer* buf = r->conn->ring_buffer_write;
    char temp[64] = {0};

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    size_t len = strftime(temp, sizeof(temp), "Date: %a, %d %b %Y %H:%M:%S GMT" CRLF, tm);
    ring_buffer_push_data(buf, temp, strlen(temp));
}


void response_append_server(request *r) 
{
    ring_buffer* buf = r->conn->ring_buffer_write;
    char temp[64] = "Server: ";
    strcat(temp, SERVER_NAME CRLF);
    ring_buffer_push_data(buf, temp, strlen(temp));
}


void response_append_content_type(request *r) 
{
    ring_buffer* buf = r->conn->ring_buffer_write;
    ssstr content_type;
    do  {
        if (r->par.err_req)  {
            content_type = SSSTR("text/html");
            break;
        }
        ssstr* v = (ssstr*)dict_get(&Mime_Dict, &r->par.url.mime_extension, NULL);
        if (v != NULL) {
            content_type = *v;
        } else {
            content_type = SSSTR("text/html");
        }
    }  while(0);
    char temp[128] = "Content-Type: ";
    strcat(temp, content_type.str);
    strcat(temp, CRLF);
    ring_buffer_push_data(buf, temp, strlen(temp));
}


void response_append_content_length(request *r) 
{
    ring_buffer* buf = r->conn->ring_buffer_write;
    int len = r->resource_size;
    
    if (len > 0)  {
        char temp[64] = {0};
        sprintf(temp, "Content-Length: %d" CRLF, len);
        ring_buffer_push_data(buf, temp, strlen(temp));
    }
}


void response_append_connection(request *r) 
{
    ring_buffer* buf = r->conn->ring_buffer_write;
    ssstr connection;
    if (r->par.keep_alive)  {
        connection = SSSTR("Connection: keep-alive" CRLF);
    }
    else  {
        connection = SSSTR("Connection: close" CRLF);
    }
    if ((time(NULL) - r->conn->time_on_connect) > server_config.connect_time_limit)   {    //if the connection take up a lot of time, tell the client close
        connection = SSSTR("Connection: close" CRLF);
    }
    ring_buffer_push_data(buf, connection.str, connection.len);
}


int Keep_Alive = 30;
void response_append_timeout(request *r) 
{
    ring_buffer* buf = r->conn->ring_buffer_write;
    if (r->par.keep_alive)  {
        char temp[64] = {0};
        sprintf(temp, "Keep-Alive: timeout=%d, max=1" CRLF, server_config.timeout_keep_alive);    //it may not work, depends on client's behaviour
        ring_buffer_push_data(buf, temp, strlen(temp));
    }
}


void response_append_crlf(request *r) 
{
    ring_buffer* buf = r->conn->ring_buffer_write;
    ring_buffer_push_data(buf, CRLF, strlen(CRLF));
}