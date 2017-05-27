#pragma once

#include "http-server.h"

void handle_get_dbus(const char *path, HttpResponse *response, void *userdata);
void handle_post_dbus(const char *path, void *body, size_t len, HttpResponse *response, void *userdata);
