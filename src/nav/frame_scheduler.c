#define _POSIX_C_SOURCE 200809L

#include "inkstand/nav/frame_scheduler.h"

#include "inkcell/ui/backend.h"
#include "inkwell/base/log.h"
#include "inkwell/runtime/loop.h"
#include "inkwell/runtime/timer.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

/*
 * Asks for one more frame in `interval_ms`, or stops asking.
 *
 * A zero delay disarms, so "still moving" and "settled" are the same call with a different
 * answer from the backend - there is no separate stop path to forget to take.
 */
static void schedule_frame(struct inkstand_frame_scheduler *scheduler, bool moving) {
    if (scheduler->timer_fd < 0) {
        return;
    }

    const int armed =
        inkwell_timer_arm_once(scheduler->timer_fd, moving ? scheduler->config.interval_ms : 0U);
    if (armed < 0) {
        inkwell_log_warn("ui", "arming the frame timer failed: %s", strerror(-armed));
    }
    scheduler->armed = moving && armed >= 0;
}

/* Whether the backend says the frame it just drew has not finished moving. */
static bool backend_moving(const struct inkstand_frame_scheduler *scheduler) {
    return scheduler->backend != NULL && scheduler->backend->animating != NULL &&
           scheduler->backend->animating(scheduler->backend_state, scheduler->backend_userdata);
}

static void present(struct inkstand_frame_scheduler *scheduler) {
    if (scheduler->backend != NULL && scheduler->backend->present != NULL) {
        scheduler->backend->present(scheduler->backend_state, scheduler->config.snapshot,
                                    scheduler->backend_userdata);
    }
}

static bool drain(struct inkstand_frame_scheduler *scheduler) {
    if (scheduler->config.drain == NULL ||
        !scheduler->config.drain(scheduler->config.source_userdata, scheduler->config.snapshot)) {
        return false;
    }
    scheduler->presented = true;
    return true;
}

static int wake_callback(int fd, uint32_t events, void *userdata) {
    (void)fd;
    struct inkstand_frame_scheduler *scheduler = (struct inkstand_frame_scheduler *)userdata;
    if (scheduler == NULL || (events & INKWELL_LOOP_IN) == 0U) {
        return 0;
    }

    if (drain(scheduler)) {
        present(scheduler);
    }

    schedule_frame(scheduler, backend_moving(scheduler));
    return 0;
}

/* Animation frames reuse the last published snapshot. Consume any real update first so
   timer/store readiness in the same loop batch cannot render stale data. */
static int timer_callback(int fd, uint32_t events, void *userdata) {
    struct inkstand_frame_scheduler *scheduler = (struct inkstand_frame_scheduler *)userdata;
    if (scheduler == NULL || (events & INKWELL_LOOP_IN) == 0U) {
        return 0;
    }

    scheduler->armed = false;
    const int64_t expired = inkwell_timer_read(fd);
    if (expired < 0) {
        inkwell_log_warn("ui", "frame timer read failed: %s", strerror((int)-expired));
    }

    if (!drain(scheduler) && scheduler->presented && scheduler->config.unchanged != NULL) {
        scheduler->config.unchanged(scheduler->config.source_userdata, scheduler->config.snapshot);
    }
    if (scheduler->presented) {
        present(scheduler);
    }

    schedule_frame(scheduler, backend_moving(scheduler));
    return 0;
}

/* The timer is optional: without it a switch lands on its target on the next frame something
   else asks for, which is a UI that works and does not animate. */
static void setup_timer(struct inkstand_frame_scheduler *scheduler) {
    scheduler->timer_fd = -1;
    struct inkwell_loop *loop = scheduler->config.loop;
    if (scheduler->backend == NULL || scheduler->backend->animating == NULL) {
        return;
    }

    const int fd = inkwell_timer_open();
    if (fd < 0) {
        inkwell_log_warn("ui", "creating the frame timer failed: %s", strerror(-fd));
        return;
    }
    if (inkwell_loop_add_fd(loop, fd, INKWELL_LOOP_IN, timer_callback, scheduler) < 0) {
        inkwell_log_warn("ui", "Failed to watch the frame timer");
        inkwell_timer_close(fd);
        return;
    }
    scheduler->timer_fd = fd;
}

static void drop_backend(struct inkstand_frame_scheduler *scheduler) {
    scheduler->backend = NULL;
    scheduler->backend_state = NULL;
    scheduler->backend_userdata = NULL;
}

int inkstand_frame_scheduler_init(struct inkstand_frame_scheduler *scheduler,
                                  const struct inkstand_frame_config *config) {
    /*
     * Each of these would start a scheduler that can never draw: with no loop nothing is
     * watched, with no wake the first refresh is published and never drained, and a zero interval
     * is arm_once()'s "disarm" - a frame recorded as armed that never fires, so animation stops
     * and settled() never comes true. Refused here rather than discovered as a blank panel.
     */
    if (scheduler == NULL || config == NULL || config->loop == NULL || config->wake_fd < 0 ||
        config->snapshot == NULL || config->drain == NULL || config->interval_ms == 0U) {
        return -EINVAL;
    }

    memset(scheduler, 0, sizeof *scheduler);
    scheduler->config = *config;
    scheduler->timer_fd = -1;
    scheduler->backend = config->backend;
    scheduler->backend_userdata = config->backend_userdata;

    if (config->backend != NULL && config->backend->init != NULL) {
        const int result =
            config->backend->init(&scheduler->backend_state, config->backend_userdata);
        if (result < 0) {
            inkwell_log_error("ui", "Backend init failed (%s): %d",
                              config->backend->name != NULL ? config->backend->name : "unknown",
                              result);
            drop_backend(scheduler);
        }
    }

    const int added = inkwell_loop_add_fd(config->loop, config->wake_fd, INKWELL_LOOP_IN,
                                          wake_callback, scheduler);
    if (added < 0) {
        inkwell_log_error("ui", "Failed to watch the store's wake: %d", added);
        if (scheduler->backend != NULL && scheduler->backend->shutdown != NULL) {
            scheduler->backend->shutdown(scheduler->backend_state, scheduler->backend_userdata);
        }
        drop_backend(scheduler);
        return added;
    }
    scheduler->registered = true;

    setup_timer(scheduler);

    /* A store only signals on change, so a program that comes up with nothing to say would sit
       on an unpainted screen indefinitely. Ask for one snapshot now so the backend draws a frame
       as soon as the loop runs. */
    if (config->refresh != NULL) {
        config->refresh(config->source_userdata);
    }

    return 0;
}

void inkstand_frame_scheduler_shutdown(struct inkstand_frame_scheduler *scheduler) {
    if (scheduler == NULL) {
        return;
    }

    if (scheduler->config.loop != NULL && scheduler->registered) {
        inkwell_loop_remove_fd(scheduler->config.loop, scheduler->config.wake_fd);
        scheduler->registered = false;
    }

    if (scheduler->timer_fd >= 0) {
        if (scheduler->config.loop != NULL) {
            inkwell_loop_remove_fd(scheduler->config.loop, scheduler->timer_fd);
        }
        inkwell_timer_close(scheduler->timer_fd);
        scheduler->timer_fd = -1;
    }
    scheduler->armed = false;

    if (scheduler->backend != NULL && scheduler->backend->shutdown != NULL) {
        scheduler->backend->shutdown(scheduler->backend_state, scheduler->backend_userdata);
    }

    drop_backend(scheduler);
    scheduler->config.loop = NULL;
}

bool inkstand_frame_scheduler_has_backend(const struct inkstand_frame_scheduler *scheduler) {
    return scheduler != NULL && scheduler->backend != NULL;
}

const char *
inkstand_frame_scheduler_backend_name(const struct inkstand_frame_scheduler *scheduler) {
    return scheduler != NULL && scheduler->backend != NULL ? scheduler->backend->name : NULL;
}

bool inkstand_frame_scheduler_presented(const struct inkstand_frame_scheduler *scheduler) {
    return scheduler != NULL && scheduler->presented;
}

/*
 * Re-arming is a one-shot timer's deadline moved back, and a window resizing faster than the
 * frame interval would move it back every time - no frame until the resizing stopped.
 */
void inkstand_frame_scheduler_request_frame(struct inkstand_frame_scheduler *scheduler) {
    if (scheduler != NULL && !scheduler->armed) {
        schedule_frame(scheduler, true);
    }
}

bool inkstand_frame_scheduler_settled(const struct inkstand_frame_scheduler *scheduler) {
    return scheduler == NULL || !scheduler->armed;
}

bool inkstand_frame_scheduler_frame(const struct inkstand_frame_scheduler *scheduler,
                                    struct inkcell_surface *out) {
    return scheduler != NULL && scheduler->backend != NULL && scheduler->backend->frame != NULL &&
           scheduler->backend->frame(scheduler->backend_state, scheduler->backend_userdata, out);
}

bool inkstand_frame_scheduler_page_rows(const struct inkstand_frame_scheduler *scheduler,
                                        uint32_t *rows) {
    if (scheduler == NULL || rows == NULL || scheduler->backend == NULL ||
        scheduler->backend->page_rows == NULL) {
        return false;
    }
    *rows = scheduler->backend->page_rows(scheduler->backend_state, scheduler->backend_userdata);
    return true;
}

bool inkstand_frame_scheduler_focus_map(const struct inkstand_frame_scheduler *scheduler,
                                        const struct inkcell_focus_map **map) {
    if (scheduler == NULL || map == NULL || scheduler->backend == NULL ||
        scheduler->backend->focus_map == NULL) {
        return false;
    }
    *map = scheduler->backend->focus_map(scheduler->backend_state, scheduler->backend_userdata);
    return true;
}
