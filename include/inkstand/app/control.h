#pragma once

/*
 * The control socket: a running program, driven by name from outside it.
 *
 * A developer - or an agent - pressing keys in a window cannot see what a press did without a
 * screenshot of the whole desktop, and cannot press anything at all on a handheld across the
 * room or in a cloud session with no display. This is a Unix socket the program listens on when
 * it is asked to - the flag or the environment variable that asks is the application's - taking
 * one command a line and answering each with one line:
 *
 *   ping              ok
 *   key NAME...       each key pressed in turn, through the path a button takes - see
 *                     inkcell_key_from_name() for the names ("a", "l1", "select", "down")
 *   wait MS           nothing, for MS milliseconds
 *   shot PATH         the frame, as a PPM at PATH, once the panel has stopped moving
 *   screen            the screen that is up, by the ASCII id the application gives it
 *   quit              the loop stops, as a quit key would stop it
 *
 * An answer is `ok`, `ok DETAIL`, or `error REASON`. A command waits for the one before it: a
 * `shot` after a `key` is a picture of what the key did.
 *
 * Off unless asked for, and 0600 when on: the socket presses keys on a running program, and
 * nothing that did not start it has any business doing that. One connection at a time;
 * a second is told `error busy`.
 *
 * `shot` needs a backend that draws pixels - inkcell's fb, sdl or headless. A backend with no
 * `frame` hook, a terminal say, has no frame to give, and a `shot` is answered with an error.
 *
 * Linux and macOS. On Windows every call refuses with -ENOTSUP (src/app/control_unavailable.c).
 */

#include "inkcell/ui/key.h"
#include "inkwell/runtime/loop.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct inkstand_frame_scheduler;

/*
 * What the socket drives, named by what it asks rather than by the program behind it.
 *
 * `frames` answers `shot`: whether the panel has stopped moving, and the frame on it. `press` is
 * `key` - the path a button takes, which only the application knows. `screen` answers `screen`
 * with an ASCII id, and may be NULL for a program with no such thing to say. `frames` may be
 * NULL too, and then every `shot` is an error; `press` may not.
 */
struct inkstand_control_host {
    struct inkstand_frame_scheduler *frames;
    void (*press)(void *userdata, enum inkcell_key key);
    const char *(*screen)(void *userdata);
    void *userdata;
};

/* sun_path is 104 bytes on a Mac and 108 on Linux; the smaller of the two, with its NUL. */
#define INKSTAND_CONTROL_PATH_MAX 104U
#define INKSTAND_CONTROL_LINE_MAX 1024U

/* How long a `shot` waits for the panel to settle before taking whatever is up. A screen that
   never settles - a map still filling tiles - is still worth a picture. */
#define INKSTAND_CONTROL_SETTLE_MS 2000U
/* How often it looks. */
#define INKSTAND_CONTROL_POLL_MS 40U

enum inkstand_control_wait {
    INKSTAND_CONTROL_READY = 0,
    INKSTAND_CONTROL_SLEEPING,
    INKSTAND_CONTROL_SETTLING,
};

struct inkstand_control {
    struct inkwell_loop *loop;
    struct inkstand_control_host host;
    char path[INKSTAND_CONTROL_PATH_MAX];
    /* The file bind() made at `path`, by device and inode: close() removes the file at the path
       only while it is still this one. */
    bool bound;
    uint64_t bound_device;
    uint64_t bound_inode;
    int listen_fd;
    int client_fd;
    int timer_fd;
    char in[INKSTAND_CONTROL_LINE_MAX];
    size_t in_len;
    enum inkstand_control_wait waiting;
    uint64_t deadline_ms;
    char shot_path[INKSTAND_CONTROL_LINE_MAX];
};

/*
 * Listens on `path`. Anything already there is refused (-EADDRINUSE) rather than removed, a
 * socket a killed run left behind included - see the note in control.c. Returns 0 or a
 * negative errno; the program runs without the socket either way.
 */
int inkstand_control_open(struct inkstand_control *control, struct inkwell_loop *loop,
                          const struct inkstand_control_host *host, const char *path);
/* Closes the connection and the socket, and removes the socket's file if the file at the path is
   still the one this bound. Safe on a control that was never opened, provided it was zeroed. */
void inkstand_control_close(struct inkstand_control *control);

/*
 * The other end: connects to `path`, sends `commands` - separated by newlines or by `;` - one at
 * a time, and prints each answer to `out`. Stops at the first `error`.
 *
 * What an application's "send these commands" flag runs, so that a device with no `nc` that
 * speaks Unix sockets - a handheld's busybox, say - is driven by the binary it already has. Returns
 * 0 when every command answered `ok`, -EPROTO when one answered `error`, or a negative errno when
 * the socket did not answer at all.
 */
int inkstand_control_send(const char *path, const char *commands, FILE *out);

#ifdef __cplusplus
}
#endif
