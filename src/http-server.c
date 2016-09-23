
#include "http-server.h"

#include <errno.h>
#include <microhttpd.h>
#include <stdlib.h>
#include <string.h>

#define _cleanup_(fn) __attribute__((__cleanup__(fn)))

typedef struct HttpRequest HttpRequest;

struct HttpServer {
        struct MHD_Daemon *daemon;
        sd_event_source *http_event;
        HttpGetHandler get_handler;
        HttpPostHandler post_handler;
        void *userdata;
};

struct HttpRequest {
        FILE *f;
        char *body;
        size_t size;
};

struct HttpResponse {
        struct MHD_Connection *connection;

        FILE *f;
        char *body;
        size_t size;

        char *content_type;

        void *user_data;
        void (*free_func)(void *);
};

static void request_completed(void *cls, struct MHD_Connection *connection,
                              void **connection_cls, enum MHD_RequestTerminationCode toe) {
        HttpRequest *request = *connection_cls;

        if (request->f)
                fclose(request->f);

        free(request->body);
        free(request);
}

static int handle_request(void *cls, struct MHD_Connection *connection,
                          const char *url, const char *method, const char *version,
                          const char *upload_data, size_t *upload_data_size,
                          void **connection_cls) {
        HttpServer *server = cls;
        HttpRequest *request = *connection_cls;
        HttpResponse *response;

        if (request == NULL) {
                request = calloc(1, sizeof(HttpRequest));
                *connection_cls = request;
                return MHD_YES;
        }

        if (*upload_data_size) {
                if (!request->f)
                        request->f = open_memstream(&request->body, &request->size);
                fwrite(upload_data, 1, *upload_data_size, request->f);
                *upload_data_size = 0;
                return MHD_YES;
        }

        if (request->f) {
                fclose(request->f);
                request->f = NULL;
        }

        MHD_suspend_connection(connection);

        response = calloc(1, sizeof(HttpResponse));
        response->connection = connection;

        if (strcmp(method, "GET") == 0 && server->get_handler)
                server->get_handler(url, response, server->userdata);
        else if (strcmp(method, "POST") == 0 && server->post_handler)
                server->post_handler(url, request->body, request->size, response, server->userdata);
        else
                http_response_end(response, 405);

        return MHD_YES;
}

static int handle_http_event(sd_event_source *event, int fd, uint32_t revents, void *userdata) {
        MHD_run(userdata);
        return 1;
}

int http_server_new(HttpServer **serverp, int port, sd_event *loop,
                    HttpGetHandler get_handler, HttpPostHandler post_handler, void *userdata) {
        _cleanup_(http_server_freep) HttpServer *server = NULL;
        int flags;
        const union MHD_DaemonInfo *info;
        int r;

        server = calloc(1, sizeof(HttpServer));
        server->get_handler = get_handler;
        server->post_handler = post_handler;
        server->userdata = userdata;

        flags = MHD_USE_DUAL_STACK |
                MHD_USE_SUSPEND_RESUME |
                MHD_USE_PEDANTIC_CHECKS |
                MHD_USE_EPOLL_LINUX_ONLY |
                MHD_USE_PIPE_FOR_SHUTDOWN;

        server->daemon = MHD_start_daemon(flags, port, NULL, NULL, handle_request, server,
                                          MHD_OPTION_NOTIFY_COMPLETED, request_completed, NULL,
                                          MHD_OPTION_END);
        if (server->daemon == NULL)
                return -EOPNOTSUPP;

        info = MHD_get_daemon_info(server->daemon, MHD_DAEMON_INFO_EPOLL_FD_LINUX_ONLY);
        if (info == NULL)
                return -EOPNOTSUPP;

        if (info->listen_fd < 0)
                return -EOPNOTSUPP;

        r = sd_event_add_io(loop, &server->http_event, info->listen_fd, EPOLLIN, handle_http_event, server->daemon);
        if (r < 0)
                return r;

        *serverp = server;
        server = NULL;

        return 0;
}

HttpServer * http_server_free(HttpServer *server) {
        if (server->http_event)
                sd_event_source_unref(server->http_event);

        if (server->daemon)
                MHD_stop_daemon(server->daemon);

        free(server);
        return NULL;
}

void http_server_freep(HttpServer **serverp) {
        if (*serverp)
                http_server_free(*serverp);
}

void http_response_end(HttpResponse *response, int status) {
        struct MHD_Response *mhd_response;
        const union MHD_ConnectionInfo *info;

        if (response->f)
                fclose(response->f);

        mhd_response = MHD_create_response_from_buffer(response->size, (void *)response->body, MHD_RESPMEM_MUST_FREE);

        if (response->content_type) {
                MHD_add_response_header(mhd_response, "Content-Type", response->content_type);
                free(response->content_type);
        }

        MHD_queue_response(response->connection, status, mhd_response);
        MHD_resume_connection(response->connection);
        info = MHD_get_connection_info(response->connection, MHD_CONNECTION_INFO_DAEMON);
        MHD_run(info->daemon);

        MHD_destroy_response(mhd_response);

        if (response->free_func)
                response->free_func(response->user_data);
        free(response);
}

FILE * http_response_get_stream(HttpResponse *response, const char *content_type) {
        if (response->f)
                return response->f;

        response->f = open_memstream(&response->body, &response->size);
        response->content_type = strdup(content_type);

        return response->f;
}

void http_response_set_user_data(HttpResponse *response, void *data, void (*free_func)(void *)) {
        if (response->free_func)
                response->free_func(response->user_data);

        response->user_data = data;
        response->free_func = free_func;
}

void * http_response_get_user_data(HttpResponse *response) {
        return response->user_data;
}
