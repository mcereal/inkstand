/*
 * The control socket where there are no Unix sockets to listen on: Windows, today. Every symbol
 * links and every call refuses, so an application calls the same functions on every system and
 * a missing socket is a warning in its log rather than an #ifdef in its code.
 */
#include "inkstand/app/control.h"

#include <errno.h>
#include <string.h>

int inkstand_control_open(struct inkstand_control *control, struct inkwell_loop *loop,
                          const struct inkstand_control_host *host, const char *path) {
    (void)loop;
    (void)host;
    (void)path;
    if (control == NULL) {
        return -EINVAL;
    }
    memset(control, 0, sizeof *control);
    return -ENOTSUP;
}

void inkstand_control_close(struct inkstand_control *control) {
    (void)control;
}

int inkstand_control_send(const char *path, const char *commands, FILE *out) {
    (void)path;
    (void)commands;
    (void)out;
    return -ENOTSUP;
}
