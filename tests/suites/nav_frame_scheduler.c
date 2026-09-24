/*
 * The frame scheduler: drain on a wake, present, and keep a timer armed only while the backend
 * is moving.
 *
 * inkstand has no store yet, so the one here is tests/support/test_store.h: the smallest thing
 * that behaves like one, which is also the whole of what the scheduler is allowed to know.
 *
 * Moved from mesh-client's ui_input suite with the scheduler, where the same cases ran against
 * its real store.
 */
#include "framework/inkstand_test.h"

#include "inkstand/nav/frame_scheduler.h"
#include "support/test_store.h"

#include "inkcell/ui/backend.h"
#include "inkcell/ui/focus.h"
#include "inkwell/runtime/loop.h"
#include "inkwell/runtime/wake.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

struct test_backend {
    int init_result;
    bool moving;
    unsigned frames;
    unsigned shutdowns;
    struct test_snapshot last;
    uint32_t page_rows;
    struct inkcell_focus_map map;
};

static int test_backend_init(void **state, void *userdata) {
    *state = userdata;
    return ((struct test_backend *)userdata)->init_result;
}

static void test_backend_shutdown(void *state, void *userdata) {
    (void)state;
    ((struct test_backend *)userdata)->shutdowns++;
}

static void test_backend_present(void *state, const void *snapshot, void *userdata) {
    (void)state;
    struct test_backend *backend = (struct test_backend *)userdata;
    backend->frames++;
    backend->last = *(const struct test_snapshot *)snapshot;
}

static bool test_backend_moving(void *state, void *userdata) {
    (void)state;
    return ((struct test_backend *)userdata)->moving;
}

static uint32_t test_backend_page_rows(void *state, void *userdata) {
    (void)state;
    return ((struct test_backend *)userdata)->page_rows;
}

static const struct inkcell_focus_map *test_backend_focus_map(void *state, void *userdata) {
    (void)state;
    return &((struct test_backend *)userdata)->map;
}

static const struct inkcell_backend test_still_backend = {
    .name = "test-still",
    .init = test_backend_init,
    .shutdown = test_backend_shutdown,
    .present = test_backend_present,
};

static const struct inkcell_backend test_moving_backend = {
    .name = "test-moving",
    .init = test_backend_init,
    .shutdown = test_backend_shutdown,
    .present = test_backend_present,
    .animating = test_backend_moving,
    .page_rows = test_backend_page_rows,
    .focus_map = test_backend_focus_map,
};

/* One loop, one store and one scheduler, opened together and closed together. */
struct test_rig {
    struct inkwell_loop loop;
    struct test_store store;
    struct test_backend backend;
    struct test_snapshot snapshot;
    struct inkstand_frame_scheduler scheduler;
};

static struct inkstand_frame_config test_config(struct test_rig *rig,
                                                const struct inkcell_backend *backend) {
    return test_store_config(&rig->store, &rig->loop, backend, &rig->backend, &rig->snapshot);
}

static int test_rig_open(struct test_rig *rig, const struct inkcell_backend *backend) {
    memset(rig, 0, sizeof *rig);
    if (inkwell_loop_init(&rig->loop) != 0) {
        return -1;
    }
    if (inkwell_wake_open(&rig->store.wake) != 0) {
        inkwell_loop_shutdown(&rig->loop);
        return -1;
    }
    const struct inkstand_frame_config config = test_config(rig, backend);
    if (inkstand_frame_scheduler_init(&rig->scheduler, &config) != 0) {
        inkwell_wake_close(&rig->store.wake);
        inkwell_loop_shutdown(&rig->loop);
        return -1;
    }
    return 0;
}

static void test_rig_close(struct test_rig *rig) {
    inkstand_frame_scheduler_shutdown(&rig->scheduler);
    inkwell_wake_close(&rig->store.wake);
    inkwell_loop_shutdown(&rig->loop);
}

/*
 * Turns the loop until the backend has seen `frames` frames, or a generous deadline passes.
 *
 * A millisecond a turn, because inkwell_loop_run()'s timeout bounds the whole call rather than
 * one wait: a turn longer than the frame interval could dispatch two frames, and a case that
 * looks at "the frame after this one" would be looking at the one after that.
 */
static bool test_rig_run_until(struct test_rig *rig, unsigned frames) {
    for (unsigned turn = 0U; turn < 2000U && rig->backend.frames < frames; ++turn) {
        (void)inkwell_loop_run(&rig->loop, 1);
    }
    return rig->backend.frames >= frames;
}

/* Every config that would start a scheduler unable to draw is refused rather than accepted:
   no loop watches nothing, no wake never drains the first refresh, and a zero interval is the
   timer's "disarm", which would leave a frame marked armed that never fires. */
INKSTAND_TEST_CASE(frame_scheduler_refuses_a_config_it_cannot_run, unit) {
    struct test_rig rig;
    memset(&rig, 0, sizeof rig);
    INKSTAND_TEST_FAIL_IF(inkwell_loop_init(&rig.loop) != 0, "loop init failed");
    INKSTAND_TEST_FAIL_IF_CLEANUP(inkwell_wake_open(&rig.store.wake) != 0,
                                  inkwell_loop_shutdown(&rig.loop), "wake open failed");

    static const char *const reasons[] = {
        "a scheduler with nowhere to put a snapshot must refuse",
        "a scheduler with no way to drain the store must refuse",
        "a scheduler with no loop must refuse",
        "a scheduler with no wake must refuse",
        "a scheduler with a zero frame interval must refuse",
    };
    const char *failure = NULL;
    for (size_t i = 0U; i < sizeof reasons / sizeof reasons[0] && failure == NULL; ++i) {
        struct inkstand_frame_config config = test_config(&rig, &test_moving_backend);
        switch (i) {
        case 0U:
            config.snapshot = NULL;
            break;
        case 1U:
            config.drain = NULL;
            break;
        case 2U:
            config.loop = NULL;
            break;
        case 3U:
            config.wake_fd = -1;
            break;
        default:
            config.interval_ms = 0U;
            break;
        }
        if (inkstand_frame_scheduler_init(&rig.scheduler, &config) != -EINVAL) {
            failure = reasons[i];
        } else if (rig.backend.shutdowns != 0U || rig.store.refreshes != 0U) {
            failure = "a refused config must not open the backend or touch the store";
        }
    }

    inkwell_wake_close(&rig.store.wake);
    inkwell_loop_shutdown(&rig.loop);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

/* A store only signals on change, so a program that comes up with nothing to say would sit on
   an unpainted screen for ever. The scheduler asks for one snapshot at init. */
INKSTAND_TEST_CASE(frame_scheduler_presents_a_first_frame_unasked, unit) {
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF(test_rig_open(&rig, &test_still_backend) != 0, "rig open failed");

    const char *failure = NULL;
    if (rig.store.refreshes != 1U) {
        failure = "init must ask the store for one snapshot";
    } else if (inkstand_frame_scheduler_presented(&rig.scheduler)) {
        failure = "nothing is presented before the loop runs";
    } else if (!test_rig_run_until(&rig, 1U)) {
        failure = "the first frame never came";
    } else if (!inkstand_frame_scheduler_presented(&rig.scheduler)) {
        failure = "a presented frame must be reported";
    }

    test_rig_close(&rig);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

/*
 * A backend that refuses to open is not a scheduler that failed to start.
 *
 * init() drops a backend whose init() refused and returns success, because a run with no UI is
 * still a run - but that means its return value cannot be read as "there is a panel", and an
 * application with a second backend to try would otherwise commit to the first and present
 * nothing. has_backend() is the question it asks instead.
 */
INKSTAND_TEST_CASE(frame_scheduler_reports_a_backend_that_would_not_open, unit) {
    struct test_rig rig;
    memset(&rig, 0, sizeof rig);
    INKSTAND_TEST_FAIL_IF(inkwell_loop_init(&rig.loop) != 0, "loop init failed");
    INKSTAND_TEST_FAIL_IF_CLEANUP(inkwell_wake_open(&rig.store.wake) != 0,
                                  inkwell_loop_shutdown(&rig.loop), "wake open failed");
    rig.backend.init_result = -ENODEV;

    const char *failure = NULL;
    const struct inkstand_frame_config refusing = test_config(&rig, &test_moving_backend);
    /* Success, deliberately: the scheduler is up and the store is being watched. */
    if (inkstand_frame_scheduler_init(&rig.scheduler, &refusing) != 0) {
        failure = "a refused backend must not stop the scheduler starting";
    } else if (inkstand_frame_scheduler_has_backend(&rig.scheduler)) {
        failure = "...but the scheduler must not claim a backend that refused";
    } else if (inkstand_frame_scheduler_backend_name(&rig.scheduler) != NULL) {
        failure = "...nor name one";
    } else if (rig.scheduler.timer_fd >= 0) {
        failure = "...nor hold a frame timer for it";
    }
    inkstand_frame_scheduler_shutdown(&rig.scheduler);
    if (failure == NULL && rig.backend.shutdowns != 0U) {
        failure = "a backend that never opened must not be shut down";
    }

    /* ...and one that opens says so, or the answer above means nothing. */
    if (failure == NULL) {
        rig.backend.init_result = 0;
        const struct inkstand_frame_config opening = test_config(&rig, &test_moving_backend);
        if (inkstand_frame_scheduler_init(&rig.scheduler, &opening) != 0) {
            failure = "scheduler init failed";
        } else if (!inkstand_frame_scheduler_has_backend(&rig.scheduler)) {
            failure = "a backend that opened must be reported as present";
        } else if (strcmp(inkstand_frame_scheduler_backend_name(&rig.scheduler), "test-moving") !=
                   0) {
            failure = "a backend that opened must be named";
        }
        inkstand_frame_scheduler_shutdown(&rig.scheduler);
    }

    inkwell_wake_close(&rig.store.wake);
    inkwell_loop_shutdown(&rig.loop);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

/* A backend that draws everything in one go asks for nothing more, and gets nothing more: a
   screen sitting still costs no wake-ups. */
INKSTAND_TEST_CASE(frame_scheduler_a_still_backend_arms_no_timer, unit) {
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF(test_rig_open(&rig, &test_still_backend) != 0, "rig open failed");

    const char *failure = NULL;
    if (rig.scheduler.timer_fd >= 0) {
        failure = "a backend that never animates needs no frame timer";
    } else if (!test_rig_run_until(&rig, 1U)) {
        failure = "the first frame never came";
    } else {
        for (unsigned turn = 0U; turn < 5U; ++turn) {
            (void)inkwell_loop_run(&rig.loop, 10);
        }
        if (rig.backend.frames != 1U) {
            failure = "a still backend must not be woken with nothing changed";
        } else if (!inkstand_frame_scheduler_settled(&rig.scheduler)) {
            failure = "a still backend is settled";
        }
    }

    test_rig_close(&rig);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

/*
 * Animation frames reuse the last snapshot and say nothing changed - but a change the store
 * published in the same batch as the timer must be drained first, or the frame would draw
 * stale data one interval longer than it had to.
 */
INKSTAND_TEST_CASE(frame_scheduler_animation_reuses_snapshot_and_consumes_changes, unit) {
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF(test_rig_open(&rig, &test_moving_backend) != 0, "rig open failed");
    rig.backend.moving = true;

    const char *failure = NULL;
    if (rig.scheduler.timer_fd < 0) {
        failure = "a backend that animates needs a frame timer";
    } else if (!test_rig_run_until(&rig, 1U) || !rig.backend.last.changed) {
        failure = "the first frame must carry the store's snapshot";
    } else if (!test_rig_run_until(&rig, 2U)) {
        failure = "a moving backend must be woken again with nothing changed";
    } else if (rig.backend.last.changed || rig.backend.last.version != 0U) {
        failure = "a timer frame must re-present the last snapshot, marked unchanged";
    } else if (inkstand_frame_scheduler_settled(&rig.scheduler)) {
        failure = "a moving backend is not settled";
    } else {
        test_store_change_quietly(&rig.store);
        const unsigned before = rig.backend.frames;
        if (!test_rig_run_until(&rig, before + 1U)) {
            failure = "the next timer frame never came";
        } else if (rig.store.pending || rig.backend.last.version != 1U ||
                   !rig.backend.last.changed) {
            failure = "a timer frame must drain a change the store has not announced yet";
        }
    }

    test_rig_close(&rig);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

/* The frame a moving backend was owed is the last one it gets once it stops: settled() is what
   something taking a picture waits for. */
INKSTAND_TEST_CASE(frame_scheduler_settles_when_the_backend_stops_moving, unit) {
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF(test_rig_open(&rig, &test_moving_backend) != 0, "rig open failed");
    rig.backend.moving = true;

    const char *failure = NULL;
    if (!test_rig_run_until(&rig, 2U)) {
        failure = "a moving backend was not woken";
    } else {
        rig.backend.moving = false;
        const unsigned last = rig.backend.frames + 1U;
        if (!test_rig_run_until(&rig, last)) {
            failure = "the frame already armed never came";
        } else if (!inkstand_frame_scheduler_settled(&rig.scheduler)) {
            failure = "a backend that stopped moving is settled";
        } else {
            for (unsigned turn = 0U; turn < 5U; ++turn) {
                (void)inkwell_loop_run(&rig.loop, 10);
            }
            if (rig.backend.frames != last) {
                failure = "a settled backend must not be woken again";
            } else {
                test_store_publish(&rig.store);
                if (!test_rig_run_until(&rig, last + 1U) || rig.backend.last.version != 1U) {
                    failure = "a settled backend must still be shown a change";
                }
            }
        }
    }

    test_rig_close(&rig);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

/* A window resizing faster than the frame interval asks for a frame on every step. Each ask
   must join the one already armed: re-arming would push the deadline back every time, and
   nothing would be drawn until the resizing stopped. */
INKSTAND_TEST_CASE(frame_scheduler_repeated_frame_requests_do_not_postpone_the_frame, unit) {
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF(test_rig_open(&rig, &test_moving_backend) != 0, "rig open failed");

    const char *failure = "a frame asked for every millisecond never came due";
    if (!test_rig_run_until(&rig, 1U)) {
        failure = "the first frame never came";
    } else {
        const unsigned before = rig.backend.frames;
        /* Asked every 1 ms for ten intervals: the first deadline has to land inside that. */
        for (unsigned step = 0U; step < rig.scheduler.config.interval_ms * 10U; ++step) {
            inkstand_frame_scheduler_request_frame(&rig.scheduler);
            (void)inkwell_loop_run(&rig.loop, 1);
            if (rig.backend.frames > before) {
                failure = NULL;
                break;
            }
        }
    }

    test_rig_close(&rig);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

/* Two facts about the frame on the panel, read back for the press that follows it - and a
   backend without the hook says so, rather than answering zero. */
INKSTAND_TEST_CASE(frame_scheduler_reads_back_the_frame_it_drew, unit) {
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF(test_rig_open(&rig, &test_moving_backend) != 0, "rig open failed");
    rig.backend.page_rows = 7U;

    const char *failure = NULL;
    uint32_t rows = 0U;
    const struct inkcell_focus_map *map = NULL;
    if (!inkstand_frame_scheduler_page_rows(&rig.scheduler, &rows) || rows != 7U) {
        failure = "page rows must come from the backend";
    } else if (!inkstand_frame_scheduler_focus_map(&rig.scheduler, &map) ||
               map != &rig.backend.map) {
        failure = "the focus map must be the backend's own";
    } else if (inkstand_frame_scheduler_frame(&rig.scheduler, NULL)) {
        failure = "a backend with no frame hook has no frame to hand out";
    }
    test_rig_close(&rig);

    if (failure == NULL) {
        INKSTAND_TEST_FAIL_IF(test_rig_open(&rig, &test_still_backend) != 0, "rig open failed");
        rows = 99U;
        if (inkstand_frame_scheduler_page_rows(&rig.scheduler, &rows) || rows != 99U) {
            failure = "a backend without page_rows must say nothing, and write nothing";
        } else if (inkstand_frame_scheduler_focus_map(&rig.scheduler, &map)) {
            failure = "a backend without focus_map must say nothing";
        }
        test_rig_close(&rig);
    }

    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

/* Shutdown closes the backend once and takes both descriptors off the loop: a store that goes
   on publishing afterwards reaches nobody. */
INKSTAND_TEST_CASE(frame_scheduler_shutdown_leaves_the_loop, unit) {
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF(test_rig_open(&rig, &test_moving_backend) != 0, "rig open failed");
    rig.backend.moving = true;

    const char *failure = NULL;
    if (!test_rig_run_until(&rig, 1U)) {
        failure = "the first frame never came";
    }
    inkstand_frame_scheduler_shutdown(&rig.scheduler);
    const unsigned frames = rig.backend.frames;
    if (failure == NULL) {
        test_store_publish(&rig.store);
        for (unsigned turn = 0U; turn < 5U; ++turn) {
            (void)inkwell_loop_run(&rig.loop, 10);
        }
        if (rig.backend.shutdowns != 1U) {
            failure = "shutdown must close the backend exactly once";
        } else if (rig.backend.frames != frames) {
            failure = "nothing may be presented after shutdown";
        } else if (rig.scheduler.timer_fd >= 0 ||
                   !inkstand_frame_scheduler_settled(&rig.scheduler)) {
            failure = "shutdown must close the frame timer";
        } else if (inkstand_frame_scheduler_has_backend(&rig.scheduler)) {
            failure = "a shut-down scheduler has no backend";
        }
    }

    inkwell_wake_close(&rig.store.wake);
    inkwell_loop_shutdown(&rig.loop);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}
