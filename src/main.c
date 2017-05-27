
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <unistd.h>

#include "systemd-compat.h"
#include "environment.h"
#include "dbus-http.h"
#include "log.h"


#define _cleanup_(fn) __attribute__((__cleanup__(fn)))

typedef struct {
        bool session_bus;
        uint16_t http_port;
} CmdArgs;


static int parse_args (int argc, char **argv, CmdArgs *cmd_args) {
        int short_arg;

        opterr = 0;

        // Default settings
        cmd_args->session_bus = false;
        cmd_args->http_port = 8080;

        while ((short_arg = getopt (argc, argv, "sp:v:h")) != -1) {
                switch (short_arg)
                {
                case 's':
                        cmd_args->session_bus = true;
                        break;

                case 'p': {
                        char *tail_ptr;
                        unsigned long port;
                        port = strtoul(optarg, &tail_ptr, 10);
                        if(cmd_args->http_port <= 32768 && *tail_ptr == 0) {
                                cmd_args->http_port = port;
                        } else {
                                puts("port must be 0..32768 (upper ports are reserved for random port numbers assigned by Linux)");
                                return -1;
                        }
                        break;
                }
                case 'v':
                        if(log_set_level_str(optarg) < 0) {
                                printf("log level must be in range: ");
                                log_print_levels();
                                puts("");
                                return -1;
                        }
                        break;
                case '?':
                case 'h':
                        puts("-s run on session DBUS");
                        puts("-p 0..32767 HTTP port (default 80)");
                        printf("-v [");
                        log_print_levels();
                        puts("]");
                        break;
                // Invalid arguments
                default:
                        return -1;
                }
        }
        return 0;
}

int main(int argc, char **argv) {
        _cleanup_(sd_event_unrefp) sd_event *loop = NULL;
        _cleanup_(sd_bus_unrefp) sd_bus *bus = NULL;
        _cleanup_(http_server_freep) HttpServer *server = NULL;
        int r;
        Environment *env = NULL;
        CmdArgs cmd_args;
        const char *session_bus = "session";
        const char *system_bus = "system";
        const char *bus_name = NULL;

        r = parse_args(argc, argv, &cmd_args);
        if (r < 0)
                goto finish;

        r = sd_event_default(&loop);
        if (r < 0)
                goto finish;

        if(cmd_args.session_bus) {
                bus_name = session_bus;
                r = sd_bus_open_user(&bus);
        }
        else {
                bus_name = system_bus;
                r = sd_bus_open_system(&bus);
        }
        if (r < 0)
                goto finish;

        log_notice("dbus-http starting on %s dbus, port %u", bus_name, cmd_args.http_port);

        r = sd_bus_attach_event(bus, loop, 0);
        if (r < 0)
                goto finish;

        // Initialize the Server Environment
        env = malloc(sizeof *env);
        if (env == NULL) {
                puts("failed to allocate memory.");
                goto finish;
        }
        env->bus = bus;
        env->dbus_prefix = "/dbus/";

        r = http_server_new(&server, cmd_args.http_port, loop, handle_get_dbus, handle_post_dbus, env);
        if (r < 0)
                goto finish;

        r = sd_event_loop(loop);
        if (r < 0)
                goto finish;

finish:
        if (r < 0)
                log_emerg("Failure: %s\n", strerror(-r));

        if(env)
                free(env);

        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
