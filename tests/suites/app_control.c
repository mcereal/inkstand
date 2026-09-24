#define _POSIX_C_SOURCE 200809L

/*
 * The control socket (include/inkstand/app/control.h), spoken to the way a sending end speaks to
 * it: a line in, a line back, over a real Unix socket on a real loop.
 *
 * What is held here is what a driver relies on without looking: that a key goes through the
 * host's press, that a bad name presses nothing, that a picture waits for the panel to stop
 * moving, and that the socket never deletes a file that is not its own.
 *
 * Moved from mesh-client's app_control suite with the socket. The host there was the client's
 * controller and nav; here it is a press that writes down what it was given and a screen id that
 * follows it, which is all the socket is allowed to know about either.
 */

#include "framework/inkstand_test.h"

#include "inkstand/app/control.h"
#include "inkstand/nav/frame_scheduler.h"
#include "support/test_store.h"

#include "inkcell/ui/backend.h"
#include "inkcell/ui/fb_draw.h"
#include "inkwell/runtime/loop.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

/* A backend with a frame to give and a switch for whether it is still moving. */
struct control_test_backend {
    bool moving;
    unsigned presents;
    uint32_t pixels[2];
};

static int control_test_init(void **state, void *userdata) {
    *state = userdata;
    return 0;
}

static void control_test_present(void *state, const void *snapshot, void *userdata) {
    (void)state;
    (void)snapshot;
    ((struct control_test_backend *)userdata)->presents += 1U;
}

static bool control_test_moving(void *state, void *userdata) {
    (void)state;
    return ((struct control_test_backend *)userdata)->moving;
}

static bool control_test_frame(void *state, void *userdata, struct inkcell_surface *out) {
    (void)state;
    struct control_test_backend *const backend = (struct control_test_backend *)userdata;
    if (backend->presents == 0U) {
        return false;
    }
    *out = (struct inkcell_surface){
        .pixels = (uint8_t *)backend->pixels,
        .size = sizeof backend->pixels,
        .width = 2U,
        .height = 1U,
        .stride = sizeof backend->pixels,
        .bytes_per_pixel = 4U,
        .format = {.bits_per_pixel = 32U},
    };
    return true;
}

/* The application behind the socket: every key it was pressed with, and a screen that R1 moves
   along - enough to tell a press that reached it from one that did not. */
struct control_test_app {
    enum inkcell_key keys[16];
    size_t key_count;
    unsigned screen;
};

static void control_test_press(void *userdata, enum inkcell_key key) {
    struct control_test_app *const app = (struct control_test_app *)userdata;
    if (app->key_count < sizeof app->keys / sizeof app->keys[0]) {
        app->keys[app->key_count++] = key;
    }
    if (key == INKCELL_KEY_R1) {
        app->screen += 1U;
    }
}

static const char *control_test_screen(void *userdata) {
    static const char *const names[] = {"home", "list", "detail"};
    const unsigned screen = ((struct control_test_app *)userdata)->screen;
    return names[screen < 3U ? screen : 2U];
}

struct control_test {
    struct inkwell_loop loop;
    struct test_store store;
    struct test_snapshot snapshot;
    struct inkstand_frame_scheduler frames;
    struct control_test_app app;
    struct inkstand_control control;
    struct control_test_backend backend;
    struct inkcell_backend vtable;
    char path[64];
    int fd;
};

static bool control_test_open_with(struct control_test *test, bool with_frame, bool with_screen) {
    memset(test, 0, sizeof *test);
    test->fd = -1;
    snprintf(test->path, sizeof test->path, "/tmp/inkstand-test-%ld.sock", (long)getpid());
    test->vtable = (struct inkcell_backend){
        .name = "test-control",
        .init = control_test_init,
        .present = control_test_present,
        .animating = control_test_moving,
        .frame = with_frame ? control_test_frame : NULL,
    };
    test->backend.pixels[0] = 0xFF112233U;
    test->backend.pixels[1] = 0xFF445566U;
    if (inkwell_loop_init(&test->loop) != 0) {
        return false;
    }
    if (test_store_open(&test->store) != 0) {
        inkwell_loop_shutdown(&test->loop);
        return false;
    }
    const struct inkstand_frame_config config = test_store_config(
        &test->store, &test->loop, &test->vtable, &test->backend, &test->snapshot);
    const struct inkstand_control_host host = {
        .frames = &test->frames,
        .press = control_test_press,
        .screen = with_screen ? control_test_screen : NULL,
        .userdata = &test->app,
    };
    if (inkstand_frame_scheduler_init(&test->frames, &config) != 0) {
        test_store_close(&test->store);
        inkwell_loop_shutdown(&test->loop);
        return false;
    }
    if (inkstand_control_open(&test->control, &test->loop, &host, test->path) != 0) {
        inkstand_frame_scheduler_shutdown(&test->frames);
        test_store_close(&test->store);
        inkwell_loop_shutdown(&test->loop);
        return false;
    }
    /* The first frame, which the scheduler asks for at init. */
    (void)inkwell_loop_run(&test->loop, 0);

    struct sockaddr_un address = {.sun_family = AF_UNIX};
    memcpy(address.sun_path, test->path, strlen(test->path) + 1U);
    test->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    return test->fd >= 0 &&
           connect(test->fd, (const struct sockaddr *)&address, sizeof address) == 0;
}

static bool control_test_open(struct control_test *test, bool with_frame) {
    return control_test_open_with(test, with_frame, true);
}

static void control_test_close(struct control_test *test) {
    if (test->fd >= 0) {
        close(test->fd);
    }
    inkstand_control_close(&test->control);
    inkstand_frame_scheduler_shutdown(&test->frames);
    test_store_close(&test->store);
    inkwell_loop_shutdown(&test->loop);
}

/* Reads one line from `fd` into `answer`, turning `loop` between reads, until the newline or
   `budget_ms` runs out. False on no answer, which some cases want. */
static bool control_test_read(struct inkwell_loop *loop, int fd, char *answer, size_t cap,
                              int budget_ms) {
    size_t got = 0U;
    for (int spent = 0; spent < budget_ms; spent += 5) {
        (void)inkwell_loop_run(loop, 5);
        struct pollfd poll_fd = {.fd = fd, .events = POLLIN};
        while (poll(&poll_fd, 1, 0) == 1 && got < cap - 1U) {
            const ssize_t n = read(fd, answer + got, 1U);
            if (n <= 0) {
                return false;
            }
            if (answer[got] == '\n') {
                answer[got] = '\0';
                return true;
            }
            got += 1U;
        }
    }
    return false;
}

/* Sends `command` - or nothing, for NULL - and waits for a whole answer. */
static bool control_test_ask(struct control_test *test, const char *command, char *answer,
                             size_t cap, int budget_ms) {
    if (command != NULL) {
        char line[256];
        const int len = snprintf(line, sizeof line, "%s\n", command);
        if (write(test->fd, line, (size_t)len) != len) {
            return false;
        }
    }
    return control_test_read(&test->loop, test->fd, answer, cap, budget_ms);
}

INKSTAND_TEST_CASE(control_refuses_a_host_it_cannot_drive, unit) {
    struct inkwell_loop loop;
    INKSTAND_TEST_FAIL_IF(inkwell_loop_init(&loop) != 0, "loop init failed");
    struct inkstand_control control;
    const struct inkstand_control_host no_press = {0};
    const int opened = inkstand_control_open(&control, &loop, &no_press, "/tmp/inkstand-never");
    inkwell_loop_shutdown(&loop);
    INKSTAND_TEST_FAIL_IF(opened != -EINVAL, "a host with no way to press a key must be refused");
    record_success(test_name);
}

INKSTAND_TEST_CASE(control_key_presses_through_the_host, unit) {
    struct control_test test;
    INKSTAND_TEST_FAIL_IF(!control_test_open(&test, true), "control socket setup failed");

    char answer[256] = {0};
    const char *failure = NULL;
    if (!control_test_ask(&test, "ping", answer, sizeof answer, 1000) ||
        strcmp(answer, "ok") != 0) {
        failure = "a ping must be answered ok";
    } else if (!control_test_ask(&test, "screen", answer, sizeof answer, 1000) ||
               strcmp(answer, "ok home") != 0) {
        failure = "screen must answer with the host's id";
    } else if (!control_test_ask(&test, "key R1 down", answer, sizeof answer, 1000) ||
               strcmp(answer, "ok") != 0) {
        failure = "keys by name must be answered ok";
    } else if (test.app.key_count != 2U || test.app.keys[0] != INKCELL_KEY_R1 ||
               test.app.keys[1] != INKCELL_KEY_DOWN) {
        failure = "...and must reach the host's press, in the order they were named";
    } else if (!control_test_ask(&test, "screen", answer, sizeof answer, 1000) ||
               strcmp(answer, "ok list") != 0) {
        failure = "screen must follow what the keys did";
    } else if (!control_test_ask(&test, "key r1 bogus", answer, sizeof answer, 1000) ||
               strncmp(answer, "error", 5U) != 0) {
        failure = "an unknown key must be answered with an error";
    } else if (test.app.key_count != 2U) {
        failure = "...and must press nothing, not even the good name before it";
    } else if (!control_test_ask(&test, "key", answer, sizeof answer, 1000) ||
               strncmp(answer, "error", 5U) != 0) {
        failure = "a key with no name must be answered with an error";
    } else if (!control_test_ask(&test, "fly", answer, sizeof answer, 1000) ||
               strncmp(answer, "error", 5U) != 0) {
        failure = "an unknown command must be answered with an error";
    }

    control_test_close(&test);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

/* A program with no screens to name says so, rather than naming one it does not have. */
INKSTAND_TEST_CASE(control_screen_without_a_name_is_an_error, unit) {
    struct control_test test;
    INKSTAND_TEST_FAIL_IF(!control_test_open_with(&test, true, false),
                          "control socket setup failed");

    char answer[256] = {0};
    const bool answered = control_test_ask(&test, "screen", answer, sizeof answer, 1000);
    control_test_close(&test);
    INKSTAND_TEST_FAIL_IF(!answered || strcmp(answer, "error no screen") != 0,
                          "a host with no screen id must answer screen with an error");
    record_success(test_name);
}

/* `wait` answers when the time is up and not before, and a command behind it waits too. */
INKSTAND_TEST_CASE(control_wait_holds_the_next_command, unit) {
    struct control_test test;
    INKSTAND_TEST_FAIL_IF(!control_test_open(&test, true), "control socket setup failed");

    static const char both[] = "wait 150\nkey a\n";
    const char *failure = NULL;
    char answer[256] = {0};
    if (write(test.fd, both, sizeof both - 1U) != (ssize_t)(sizeof both - 1U)) {
        failure = "could not send";
    } else if (control_test_read(&test.loop, test.fd, answer, sizeof answer, 50)) {
        failure = "a wait must not answer before its time";
    } else if (test.app.key_count != 0U) {
        failure = "...and the key behind it must not be pressed yet";
    } else if (!control_test_read(&test.loop, test.fd, answer, sizeof answer, 1000) ||
               strcmp(answer, "ok") != 0) {
        failure = "a wait must answer ok once its time is up";
    } else if (!control_test_read(&test.loop, test.fd, answer, sizeof answer, 1000) ||
               strcmp(answer, "ok") != 0 || test.app.key_count != 1U) {
        failure = "...and the key behind it must then be pressed";
    }

    control_test_close(&test);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

/* One driver at a time: a second connection is told it is not the one, and the first carries on. */
INKSTAND_TEST_CASE(control_a_second_connection_is_busy, unit) {
    struct control_test test;
    INKSTAND_TEST_FAIL_IF(!control_test_open(&test, true), "control socket setup failed");

    struct sockaddr_un address = {.sun_family = AF_UNIX};
    memcpy(address.sun_path, test.path, strlen(test.path) + 1U);
    const int second = socket(AF_UNIX, SOCK_STREAM, 0);
    const char *failure = NULL;
    char answer[256] = {0};
    /* The first connection is accepted on a loop turn; make sure it has been. */
    if (!control_test_ask(&test, "ping", answer, sizeof answer, 1000)) {
        failure = "the first connection must be answered";
    } else if (second < 0 ||
               connect(second, (const struct sockaddr *)&address, sizeof address) != 0) {
        failure = "could not connect a second time";
    } else if (!control_test_read(&test.loop, second, answer, sizeof answer, 1000) ||
               strcmp(answer, "error busy") != 0) {
        failure = "a second connection must be told the socket is busy";
    } else if (!control_test_ask(&test, "ping", answer, sizeof answer, 1000) ||
               strcmp(answer, "ok") != 0) {
        failure = "...and the first must carry on";
    }
    if (second >= 0) {
        close(second);
    }

    control_test_close(&test);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

/* A picture taken mid-slide is a picture of neither screen, so `shot` holds its answer while the
   backend says it is still moving and takes the frame once it stops. */
INKSTAND_TEST_CASE(control_shot_waits_for_the_panel_to_settle, unit) {
    struct control_test test;
    INKSTAND_TEST_FAIL_IF(!control_test_open(&test, true), "control socket setup failed");

    char shot[64];
    snprintf(shot, sizeof shot, "/tmp/inkstand-test-%ld.ppm", (long)getpid());
    char command[128];
    snprintf(command, sizeof command, "shot %s", shot);

    /* Moving from the next frame on, which a publish asks for. */
    test.backend.moving = true;
    test_store_publish(&test.store);

    char answer[256] = {0};
    const char *failure = NULL;
    if (control_test_ask(&test, command, answer, sizeof answer, 300)) {
        failure = "a shot must not be taken while the panel is moving";
    } else {
        test.backend.moving = false;
        char expected[128];
        snprintf(expected, sizeof expected, "ok %s", shot);
        if (!control_test_ask(&test, NULL, answer, sizeof answer, 1000) ||
            strcmp(answer, expected) != 0) {
            failure = "...and must be taken once it stops";
        } else {
            FILE *file = fopen(shot, "rb");
            unsigned char data[32] = {0};
            const size_t len = file != NULL ? fread(data, 1U, sizeof data, file) : 0U;
            if (file != NULL) {
                fclose(file);
            }
            static const unsigned char ppm[] = {'P', '6',  '\n', '2',  ' ',  '1',  '\n', '2', '5',
                                                '5', '\n', 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
            if (len != sizeof ppm || memcmp(data, ppm, len) != 0) {
                failure = "...as the backend's frame, written as a PPM";
            }
        }
    }
    unlink(shot);

    control_test_close(&test);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

INKSTAND_TEST_CASE(control_shot_without_a_frame_is_an_error, unit) {
    struct control_test test;
    INKSTAND_TEST_FAIL_IF(!control_test_open(&test, false), "control socket setup failed");

    char answer[256] = {0};
    const bool answered =
        control_test_ask(&test, "shot /tmp/inkstand-test-none.ppm", answer, sizeof answer, 1000);
    control_test_close(&test);
    INKSTAND_TEST_FAIL_IF(!answered || strncmp(answer, "error", 5U) != 0,
                          "a backend with no frame must answer a shot with an error");
    record_success(test_name);
}

/* The path is the caller's choice, and a typo in it must not cost them a file. */
INKSTAND_TEST_CASE(control_does_not_replace_a_file_that_is_not_a_socket, unit) {
    char path[64];
    snprintf(path, sizeof path, "/tmp/inkstand-test-file-%ld", (long)getpid());
    const int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    INKSTAND_TEST_FAIL_IF(fd < 0, "could not create the file");
    close(fd);

    struct inkwell_loop loop;
    struct control_test_app app = {0};
    struct inkstand_control control;
    const struct inkstand_control_host host = {.press = control_test_press, .userdata = &app};
    INKSTAND_TEST_FAIL_IF_CLEANUP(inkwell_loop_init(&loop) != 0, unlink(path), "loop init failed");

    const int opened = inkstand_control_open(&control, &loop, &host, path);
    struct stat info;
    const bool kept = stat(path, &info) == 0 && S_ISREG(info.st_mode);
    unlink(path);
    if (opened == 0) {
        inkstand_control_close(&control);
    }
    inkwell_loop_shutdown(&loop);

    INKSTAND_TEST_FAIL_IF(opened >= 0, "a regular file at the path must be refused");
    INKSTAND_TEST_FAIL_IF(!kept, "...and left where it was");
    record_success(test_name);
}

/* Closing removes the socket it bound - the one file it is allowed to remove. */
INKSTAND_TEST_CASE(control_close_removes_its_own_socket, unit) {
    struct control_test test;
    INKSTAND_TEST_FAIL_IF(!control_test_open(&test, true), "control socket setup failed");
    char path[sizeof test.path];
    memcpy(path, test.path, sizeof path);
    struct stat info;
    const bool was_there = stat(path, &info) == 0 && S_ISSOCK(info.st_mode);
    const bool private_to_us = was_there && (info.st_mode & 0077) == 0;
    control_test_close(&test);
    const bool gone = stat(path, &info) != 0 && errno == ENOENT;
    INKSTAND_TEST_FAIL_IF(!was_there, "an open control must be a socket at its path");
    INKSTAND_TEST_FAIL_IF(!private_to_us, "...readable and writable by its owner alone");
    INKSTAND_TEST_FAIL_IF(!gone, "closing must remove the socket it bound");
    record_success(test_name);
}

/* ...and only while it is still the file at the path: something that replaced it since is not the
   control's to delete. */
INKSTAND_TEST_CASE(control_close_leaves_a_replacement_alone, unit) {
    struct control_test test;
    INKSTAND_TEST_FAIL_IF(!control_test_open(&test, true), "control socket setup failed");
    char path[sizeof test.path];
    memcpy(path, test.path, sizeof path);

    const char *failure = NULL;
    if (unlink(path) != 0) {
        failure = "could not unlink the socket";
    } else {
        const int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (fd < 0) {
            failure = "could not put a file at the path";
        } else {
            close(fd);
        }
    }
    control_test_close(&test);
    struct stat info;
    if (failure == NULL && !(stat(path, &info) == 0 && S_ISREG(info.st_mode))) {
        failure = "closing must not remove a file that replaced the socket";
    }
    unlink(path);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

/*
 * The sending end, against a listening one. The sender blocks and the listener needs its loop
 * turned, so the listener is a child process: it opens the socket, runs its loop until `quit`
 * stops it (or three seconds pass), and exits. The parent retries the first send until the child
 * is listening.
 */
static void control_test_serve(const char *path) {
    struct inkwell_loop loop;
    struct test_store store;
    struct test_snapshot snapshot;
    struct inkstand_frame_scheduler frames;
    struct control_test_app app = {0};
    struct inkstand_control control;
    const struct inkcell_backend still = {.name = "test-still", .present = control_test_present};
    struct control_test_backend backend = {0};
    if (inkwell_loop_init(&loop) != 0 || test_store_open(&store) != 0) {
        _exit(2);
    }
    const struct inkstand_frame_config config =
        test_store_config(&store, &loop, &still, &backend, &snapshot);
    const struct inkstand_control_host host = {
        .frames = &frames,
        .press = control_test_press,
        .screen = control_test_screen,
        .userdata = &app,
    };
    if (inkstand_frame_scheduler_init(&frames, &config) != 0 ||
        inkstand_control_open(&control, &loop, &host, path) != 0) {
        _exit(3);
    }
    /* Bounded, and cut short by `quit`, which stops the loop. */
    (void)inkwell_loop_run(&loop, 3000);
    inkstand_control_close(&control);
    inkstand_frame_scheduler_shutdown(&frames);
    _exit(app.key_count == 1U ? 0 : 4);
}

INKSTAND_TEST_CASE(control_send_drives_a_listening_socket, unit) {
    char path[64];
    snprintf(path, sizeof path, "/tmp/inkstand-test-send-%ld.sock", (long)getpid());
    fflush(NULL);
    const pid_t child = fork();
    INKSTAND_TEST_FAIL_IF(child < 0, "fork failed");
    if (child == 0) {
        control_test_serve(path);
    }

    char *text = NULL;
    size_t text_len = 0U;
    FILE *out = open_memstream(&text, &text_len);
    const char *failure = NULL;
    int sent = -ENOENT;
    for (int tries = 0; tries < 300 && (sent == -ENOENT || sent == -ECONNREFUSED); ++tries) {
        if (tries > 0) {
            (void)poll(NULL, 0, 10);
        }
        sent = inkstand_control_send(path, "ping; key a; screen; fly; ping", out);
    }
    fflush(out);
    static const char expected[] = "ping: ok\nkey a: ok\nscreen: ok home\n"
                                   "fly: error unknown command 'fly'\n";
    if (sent != -EPROTO) {
        failure = "a command answered with an error must make the send -EPROTO";
    } else if (text == NULL || strcmp(text, expected) != 0) {
        failure = "every answer up to the error must be printed, and nothing after it";
    } else {
        FILE *sink = fopen("/dev/null", "w");
        if (sink == NULL || inkstand_control_send(path, "quit", sink) != 0) {
            failure = "a second connection, after the first hung up, must be answered";
        }
        if (sink != NULL) {
            fclose(sink);
        }
    }
    fclose(out);
    free(text);

    int status = 0;
    if (waitpid(child, &status, 0) != child) {
        failure = failure != NULL ? failure : "the listening child was lost";
    } else if (failure == NULL && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) {
        failure = "the listener must press the one key it was sent, and stop on quit";
    }
    unlink(path);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

INKSTAND_TEST_CASE(control_send_to_nothing_says_so, unit) {
    FILE *sink = fopen("/dev/null", "w");
    INKSTAND_TEST_FAIL_IF(sink == NULL, "no /dev/null");
    const int sent = inkstand_control_send("/tmp/inkstand-test-absent.sock", "ping", sink);
    fclose(sink);
    INKSTAND_TEST_FAIL_IF(sent >= 0, "a socket that is not there must not be answered ok");
    record_success(test_name);
}
