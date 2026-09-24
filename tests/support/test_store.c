#include "support/test_store.h"

#include <string.h>

int test_store_open(struct test_store *store) {
    memset(store, 0, sizeof *store);
    return inkwell_wake_open(&store->wake);
}

void test_store_close(struct test_store *store) {
    inkwell_wake_close(&store->wake);
}

bool test_store_drain(void *userdata, void *snapshot) {
    struct test_store *store = (struct test_store *)userdata;
    (void)inkwell_wake_drain(&store->wake);
    if (!store->pending) {
        return false;
    }
    store->pending = false;
    struct test_snapshot *out = (struct test_snapshot *)snapshot;
    out->version = store->version;
    out->changed = true;
    return true;
}

void test_store_unchanged(void *userdata, void *snapshot) {
    (void)userdata;
    ((struct test_snapshot *)snapshot)->changed = false;
}

void test_store_refresh(void *userdata) {
    struct test_store *store = (struct test_store *)userdata;
    store->refreshes++;
    store->pending = true;
    (void)inkwell_wake_signal(&store->wake);
}

void test_store_change_quietly(struct test_store *store) {
    store->version++;
    store->pending = true;
}

void test_store_publish(struct test_store *store) {
    test_store_change_quietly(store);
    (void)inkwell_wake_signal(&store->wake);
}

struct inkstand_frame_config test_store_config(struct test_store *store, struct inkwell_loop *loop,
                                               const struct inkcell_backend *backend,
                                               void *backend_userdata,
                                               struct test_snapshot *snapshot) {
    const struct inkstand_frame_config config = {
        .loop = loop,
        .backend = backend,
        .backend_userdata = backend_userdata,
        .snapshot = snapshot,
        .wake_fd = store->wake.fd,
        .drain = test_store_drain,
        .unchanged = test_store_unchanged,
        .refresh = test_store_refresh,
        .source_userdata = store,
        .interval_ms = 5U,
    };
    return config;
}
