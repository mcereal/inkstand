#pragma once

/*
 * The smallest thing that behaves like a store, as a frame scheduler sees one.
 *
 * inkstand has no store yet. What a scheduler is allowed to know about one is a wake that says
 * something was published, a drain that fills a snapshot, a hook for a frame that changed
 * nothing and a nudge to publish once - so this is those four, over a counter that stands for
 * "something" and a snapshot that says whether this frame changed it.
 *
 * Shared by the scheduler's suite and the control socket's, which needs a scheduler to drive.
 */

#include "inkstand/nav/frame_scheduler.h"

#include "inkwell/runtime/wake.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct test_snapshot {
    uint32_t version;
    bool changed;
};

struct test_store {
    struct inkwell_wake wake;
    uint32_t version;
    bool pending;
    unsigned refreshes;
};

/* Opens the wake; 0 or a negative errno. */
int test_store_open(struct test_store *store);
void test_store_close(struct test_store *store);

/* The four hooks, for a struct inkstand_frame_config whose source_userdata is the store and
   whose snapshot is a struct test_snapshot. */
bool test_store_drain(void *userdata, void *snapshot);
void test_store_unchanged(void *userdata, void *snapshot);
void test_store_refresh(void *userdata);

/* A change the store has not announced - what a publish in the same loop batch as a timer frame
   looks like from the timer's side. */
void test_store_change_quietly(struct test_store *store);
/* A change, announced. */
void test_store_publish(struct test_store *store);

/* A config over `store`, with the loop, the backend and the snapshot filled in and a 5 ms
   interval. */
struct inkstand_frame_config test_store_config(struct test_store *store, struct inkwell_loop *loop,
                                               const struct inkcell_backend *backend,
                                               void *backend_userdata,
                                               struct test_snapshot *snapshot);

#ifdef __cplusplus
}
#endif
