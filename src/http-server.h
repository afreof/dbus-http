#pragma once

#include <stdio.h>
#include <stdint.h>
#include <systemd/sd-event.h>

typedef struct HttpServer HttpServer;
typedef struct HttpResponse HttpResponse;

typedef void (*HttpGetHandler)(const char *path, HttpResponse *response, void *userdata);
typedef void (*HttpPostHandler)(const char *path, void *data, size_t len, HttpResponse *response, void *userdata);

int http_server_new(HttpServer **serverp, uint16_t port, sd_event *loop,
                    HttpGetHandler get_handler, HttpPostHandler post_handler, void *userdata);
HttpServer * http_server_free(HttpServer *server);
void http_server_freep(HttpServer **serverp);

void http_response_end(HttpResponse *response, int status);
FILE * http_response_get_stream(HttpResponse *response, const char *content_type);
void http_response_set_user_data(HttpResponse *response, void *data, void (*free_func)(void *));
void * http_response_get_user_data(HttpResponse *response);
