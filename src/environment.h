#pragma once

typedef struct {
        sd_bus *bus;
        const char *dbus_prefix;
} Environment;
