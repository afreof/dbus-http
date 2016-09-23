
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#include "dbus.h"
#include "json.h"
#include "http-server.h"

#define _cleanup_(fn) __attribute__((__cleanup__(fn)))

typedef struct {
        char *destination;
        char *object;
        JsonValue *json;
        DBusNode *node;
        DBusMethod *method;
} MethodCallRequest;

static int bus_message_element_to_json(sd_bus_message *message, JsonValue **jsonp);

static inline void freep(void *p) {
        free(*(void **)p);
}

static void method_call_request_free(MethodCallRequest *request) {
        free(request->destination);
        free(request->object);
        if (request->json)
                json_value_free(request->json);
        if (request->node)
                dbus_node_free(request->node);
        free(request);
}

static void http_response_end_json(HttpResponse *response, int status, JsonValue *reply) {
        FILE *f;

        f = http_response_get_stream(response, "application/json");
        json_print(reply, f);
        http_response_end(response, status);
}

static void http_response_end_error(HttpResponse *response, int status, const char *name, const char *message) {
        _cleanup_(json_value_freep) JsonValue *reply;

        reply = json_object_new();
        json_object_insert_string(reply, "error", name);

        if (message)
                json_object_insert_string(reply, "message", message);

        http_response_end_json(response, status, reply);
}

static void http_response_end_dbus_error(HttpResponse *response, const sd_bus_error *error) {
        int status;

        if (strcmp(error->name, "org.freedesktop.DBus.Error.UnknownMethod") == 0 ||
            strcmp(error->name, "org.freedesktop.DBus.Error.UnknownObject") == 0 ||
            strcmp(error->name, "org.freedesktop.DBus.Error.UnknownInterface") == 0 ||
            strcmp(error->name, "org.freedesktop.DBus.Error.UnknownProperty") == 0 ||
            strcmp(error->name, "org.freedesktop.DBus.Error.InvalidSignature") == 0 ||
            strcmp(error->name, "org.freedesktop.DBus.Error.InvalidArgs") == 0)
                status = 400;
        else if (strcmp(error->name, "org.freedesktop.DBus.Error.AccessDenied") == 0)
                status = 403;
        else if (strcmp(error->name, "org.freedesktop.DBus.Error.ServiceUnknown") == 0 ||
                 strcmp(error->name, "org.freedesktop.DBus.Error.NameHasNoOwner") == 0)
                status = 404;
        else if (strcmp(error->name, "org.freedesktop.DBus.Error.NoReply") == 0 ||
                 strcmp(error->name, "org.freedesktop.DBus.Error.Timeout") == 0)
                status = 408;
        else
                status = 500;

        http_response_end_error(response, status, error->name, error->message);
}

static int bus_message_dict_entry_to_json(sd_bus_message *message, char **keyp, JsonValue **valuep) {
        char type;
        const char *contents;
        const char *key;
        _cleanup_(json_value_freep) JsonValue *value = NULL;
        int r;

        r = sd_bus_message_peek_type(message, &type, &contents);
        if (r < 0)
                return r;

        r = sd_bus_message_enter_container(message, type, contents);
        if (r < 0)
                return r;

        switch (contents[0]) {
                case SD_BUS_TYPE_STRING:
                case SD_BUS_TYPE_OBJECT_PATH:
                case SD_BUS_TYPE_SIGNATURE:
                        r = sd_bus_message_read_basic(message, contents[0], &key);
                        if (r < 0)
                                return r;
                        break;

                default:
                        return -ENOTSUP;
        }

        r = bus_message_element_to_json(message, &value);
        if (r < 0)
                return r;

        r = sd_bus_message_exit_container(message);
        if (r < 0)
                return r;

        *keyp = strdup(key);
        key = NULL;

        *valuep = value;
        value = NULL;

        return 0;
}

static int bus_message_element_to_json(sd_bus_message *message, JsonValue **jsonp) {
        _cleanup_(json_value_freep) JsonValue *json = NULL;
        const char *contents = NULL;
        char type;
        int r;

        r = sd_bus_message_peek_type(message, &type, &contents);
        if (r < 0)
                return r;
        if (r == 0)
                return 0;

        switch (type) {
                case SD_BUS_TYPE_BOOLEAN: {
                        uint8_t b;
                        r = sd_bus_message_read_basic(message, type, &b);
                        if (r < 0)
                                return r;
                        json = json_boolean_new(b);
                        break;
                }

                case SD_BUS_TYPE_BYTE: {
                        uint8_t num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_INT16: {
                        int16_t num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_UINT16: {
                        uint16_t num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_INT32: {
                        int32_t num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_UINT32: {
                        uint32_t num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_INT64: {
                        int64_t num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_UINT64: {
                        uint64_t num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_DOUBLE: {
                        double num;
                        r = sd_bus_message_read_basic(message, type, &num);
                        if (r < 0)
                                return r;
                        json = json_number_new(num);
                        break;
                }

                case SD_BUS_TYPE_STRING:
                case SD_BUS_TYPE_OBJECT_PATH:
                case SD_BUS_TYPE_SIGNATURE: {
                        const char *string;
                        r = sd_bus_message_read_basic(message, type, &string);
                        if (r < 0)
                                return r;
                        json = json_string_new(string);
                        break;
                }

                case SD_BUS_TYPE_ARRAY:
                case SD_BUS_TYPE_STRUCT:
                        r = sd_bus_message_enter_container(message, type, contents);
                        if (r < 0)
                                return r;

                        if (contents[0] == SD_BUS_TYPE_DICT_ENTRY_BEGIN) {
                                json = json_object_new();
                                while (!sd_bus_message_at_end(message, false)) {
                                        _cleanup_(freep) char *key = NULL;
                                        _cleanup_(json_value_freep) JsonValue *element = NULL;

                                        r = bus_message_dict_entry_to_json(message, &key, &element);
                                        if (r < 0)
                                                return r;

                                        r = json_object_insert(json, key, element);
                                        if (r < 0)
                                                return r;

                                        element = NULL;
                                }
                        } else {
                                json = json_array_new();
                                while (!sd_bus_message_at_end(message, false)) {
                                        _cleanup_(json_value_freep) JsonValue *element = NULL;

                                        r = bus_message_element_to_json(message, &element);
                                        if (r < 0)
                                                return r;

                                        r = json_array_append(json, element);
                                        if (r < 0)
                                                return r;

                                        element = NULL;
                                }
                        }

                        r = sd_bus_message_exit_container(message);
                        if (r < 0)
                                return r;
                        break;

                case SD_BUS_TYPE_VARIANT:
                        r = sd_bus_message_enter_container(message, type, contents);
                        if (r < 0)
                                return r;

                        r = bus_message_element_to_json(message, &json);
                        if (r < 0)
                                return r;

                        r = sd_bus_message_exit_container(message);
                        if (r < 0)
                                return r;
                        break;

                case SD_BUS_TYPE_UNIX_FD:
                        return -ENOTSUP;
        }

        *jsonp = json;
        json = NULL;

        return 0;
}

static int bus_message_to_json(sd_bus_message *message, JsonValue **jsonp, DBusMethod *method) {
        _cleanup_(json_value_freep) JsonValue *json = NULL;
        int r;

        json = json_object_new();

        for (size_t i = 0; i < method->n_out_args; i++) {
                _cleanup_(json_value_freep) JsonValue *element = NULL;

                if (sd_bus_message_at_end(message, false))
                        return -EINVAL;

                r = bus_message_element_to_json(message, &element);
                if (r < 0)
                        return r;

                r = json_object_insert(json, method->out_args[i]->name, element);
                if (r < 0)
                        return r;

                element = NULL;

        }

        if (!sd_bus_message_at_end(message, false))
                return -EINVAL;

        *jsonp = json;
        json = NULL;

        return 0;
}

static int bus_message_append_number(sd_bus_message *message, char type, double number) {
        switch (type) {
                case SD_BUS_TYPE_UINT32: {
                        uint32_t num = number;
                        sd_bus_message_append_basic(message, type, &num);
                        break;
                }

                default:
                        return -EINVAL;
        }

        return 0;
}

static int bus_message_append_from_json(sd_bus_message *message, JsonValue *json, const char *type) {
        if (strchr("ynqiuxtd", *type) != NULL) {
                if (json_value_get_type(json) != JSON_TYPE_NUMBER)
                        return -EINVAL;

                return bus_message_append_number(message, *type, json_value_get_number(json));
        }

        switch (*type) {
                case SD_BUS_TYPE_ARRAY:
                case SD_BUS_TYPE_STRUCT:
                case SD_BUS_TYPE_DICT_ENTRY:
                case SD_BUS_TYPE_VARIANT:
                case SD_BUS_TYPE_UNIX_FD:
                        return -ENOTSUP;

                case SD_BUS_TYPE_BOOLEAN: {
                        bool b;

                        if (json_value_get_type(json) == JSON_TYPE_TRUE)
                                b = true;
                        else if (json_value_get_type(json) == JSON_TYPE_FALSE)
                                b = false;
                        else
                                return -EINVAL;

                        return sd_bus_message_append_basic(message, 'b', &b);
                }

                case SD_BUS_TYPE_STRING:
                case SD_BUS_TYPE_OBJECT_PATH:
                case SD_BUS_TYPE_SIGNATURE: {
                        if (json_value_get_type(json) != JSON_TYPE_STRING)
                                return -EINVAL;

                        return sd_bus_message_append_basic(message, *type, json_value_get_string(json));
                }
        }

        return -EINVAL;
}

static int bus_message_append_args_from_json(sd_bus_message *message, DBusMethod *method, JsonValue *args) {
        size_t n_args;
        int r;

        n_args = json_array_get_length(args);

        if (method->n_in_args != n_args)
                return -EINVAL;

        for (size_t i = 0; i < n_args; i++) {
                JsonValue *arg;

                json_array_get(args, i, &arg, 0);

                r = bus_message_append_from_json(message, arg, method->in_args[i]->type);
                if (r < 0)
                        return r;
        }

        return 0;
}

static int get_properties_finished(sd_bus_message *message, void *userdata, sd_bus_error *ret_error) {
        HttpResponse *response = userdata;
        const sd_bus_error *error;
        _cleanup_(json_value_freep) JsonValue *reply = NULL;
        int r;

        error = sd_bus_message_get_error(message);
        if (error) {
                http_response_end_dbus_error(response, error);
                return 0;
        }

        r = bus_message_element_to_json(message, &reply);
        if (r < 0) {
                http_response_end(response, 500);
                return 0;
        }

        http_response_end_json(response, 200, reply);
        return 0;
}

static int method_call_finished(sd_bus_message *message, void *userdata, sd_bus_error *ret_error) {
        HttpResponse *response = userdata;
        MethodCallRequest *request = http_response_get_user_data(response);
        const sd_bus_error *error;
        _cleanup_(json_value_freep) JsonValue *reply = NULL;
        int r;

        error = sd_bus_message_get_error(message);
        if (error) {
                http_response_end_dbus_error(response, error);
                return 0;
        }

        r = bus_message_to_json(message, &reply, request->method);
        if (r < 0) {
                http_response_end(response, 500);
                return 0;
        }

        http_response_end_json(response, 200, reply);
        return 0;
}

static int introspect_finished(sd_bus_message *message, void *userdata, sd_bus_error *ret_error) {
        HttpResponse *response = userdata;
        MethodCallRequest *request = http_response_get_user_data(response);
        const sd_bus_error *error;
        const char *xml;
        _cleanup_(sd_bus_message_unrefp) sd_bus_message *method_message = NULL;
        const char *interface;
        const char *method_name;
        JsonValue *args;
        int r;

        error = sd_bus_message_get_error(message);
        if (error) {
                http_response_end_dbus_error(response, error);
                return 0;
        }

        r = sd_bus_message_read(message, "s", &xml);
        if (r < 0) {
                http_response_end(response, 500);
                return 0;
        }

        r = dbus_node_new_from_xml(&request->node, xml);
        if (r < 0) {
                http_response_end(response, 500);
                return 0;
        }

        if (!json_object_lookup_string(request->json, "interface", &interface) ||
            !json_object_lookup_string(request->json, "method", &method_name) ||
            !json_object_lookup(request->json, "arguments", &args, JSON_TYPE_ARRAY)) {
                http_response_end_error(response, 400, "Invalid request", NULL);
                return 0;
        }

        request->method = dbus_node_find_method(request->node, interface, method_name);
        if (!request->method) {
                http_response_end_error(response, 400, "No such method", NULL);
                return 0;
        }

        r = sd_bus_message_new_method_call(sd_bus_message_get_bus(message),
                                           &method_message, request->destination, request->object, interface, method_name);
        if (r < 0) {
                http_response_end(response, 500);
                return 0;
        }

        r = bus_message_append_args_from_json(method_message, request->method, args);
        if (r == -EINVAL) {
                http_response_end_error(response, 400, "Invalid request", NULL);
                return 0;
        } else if (r < 0) {
                http_response_end(response, 500);
                return 0;
        }

        r = sd_bus_call_async(sd_bus_message_get_bus(message), NULL, method_message, method_call_finished, response, 0);

        return 0;
}

static int parse_url(const char *url, char **namep, char **objectp) {
        const char *p;
        char *name;
        char *object;

        if (url[0] != '/')
                return -EINVAL;

        p = strchr(url + 1, '/');
        if (p) {
                name = strndup(url + 1, p - url - 1);
                object = strdup(p);
        } else {
                name = strdup(url + 1);
                object = strdup("/");
        }

        if (namep)
                *namep = name;

        if (objectp)
                *objectp = object;

        return 0;
}

static void handle_get(const char *path, HttpResponse *response, void *userdata) {
        sd_bus *bus = userdata;
        _cleanup_(freep) char *name = NULL;
        _cleanup_(freep) char *object = NULL;
        int r;

        r = parse_url(path, &name, &object);
        if (r < 0) {
                http_response_end(response, 400);
                return;
        }

        r = sd_bus_call_method_async(bus, NULL, name, object, "org.freedesktop.DBus.Properties", "GetAll",
                                     get_properties_finished, response, "s", "");
        if (r == -EINVAL)
                http_response_end(response, 400);
        else if (r < 0)
                http_response_end(response, 500);
}

static void handle_post(const char *path, void *body, size_t len, HttpResponse *response, void *userdata) {
        sd_bus *bus = userdata;
        MethodCallRequest *request;
        int r;

        if (!body) {
                http_response_end(response, 400);
                return;
        }

        request = calloc(1, sizeof(MethodCallRequest));
        http_response_set_user_data(response, request, (void (*)(void *))method_call_request_free);

        r = parse_url(path, &request->destination, &request->object);
        if (r < 0) {
                http_response_end(response, 400);
                return;
        }

        r = json_parse(body, &request->json, JSON_TYPE_OBJECT);
        if (r < 0) {
                http_response_end(response, 400);
                return;
        }

        r = sd_bus_call_method_async(bus, NULL, request->destination, request->object,
                                     "org.freedesktop.DBus.Introspectable", "Introspect",
                                     introspect_finished, response, NULL);
        if (r < 0) {
                http_response_end(response, 400);
                return;
        }
}

int main(int argc, char **argv) {
        _cleanup_(sd_event_unrefp) sd_event *loop = NULL;
        _cleanup_(sd_bus_unrefp) sd_bus *bus = NULL;
        _cleanup_(http_server_freep) HttpServer *server = NULL;
        int r;

        r = sd_event_default(&loop);
        if (r < 0)
                return EXIT_FAILURE;

        r = sd_bus_open_system(&bus);
        if (r < 0)
                return EXIT_FAILURE;

        r = sd_bus_attach_event(bus, loop, 0);
        if (r < 0)
                return EXIT_FAILURE;

        r = http_server_new(&server, 8080, loop, handle_get, handle_post, bus);
        if (r < 0)
                return EXIT_FAILURE;

        r = sd_event_loop(loop);
        if (r < 0)
                return EXIT_FAILURE;

        return EXIT_SUCCESS;
}
