/*
 * The headless half's link line, held before anything is in it.
 *
 * state/ and persist/ have no code yet, so what can be tested is the promise the build makes on
 * their behalf: that inkstand::core resolves inkwell - one copy of it, the one this tree pins -
 * and nothing that draws. This suite is compiled with INKSTAND_WITH_UI=OFF as well, where inkcell
 * is not configured at all, so an include of an inkcell header creeping in here is a build
 * failure on that job rather than a line somebody has to spot.
 *
 * It is the first suite a real one displaces: once the store lands, its own cases say the same
 * thing with more to say.
 */
#include "framework/inkstand_test.h"

#include "inkwell/runtime/loop.h"

INKSTAND_TEST_CASE(core_links_the_inkwell_loop, unit) {
    struct inkwell_loop loop;
    INKSTAND_TEST_FAIL_IF(inkwell_loop_init(&loop) != 0, "an inkwell loop should initialise");
    inkwell_loop_shutdown(&loop);
    record_success(test_name);
}
