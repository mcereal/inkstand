/*
 * The two-answer dialog: opens on Cancel, keeps its cursor between its own two answers, and closes
 * on an answer.
 *
 * mesh-client's capture suite still holds the dialog against the layouts its own frames draw - the
 * two answers side by side on a wide panel and stacked on a narrow one - and runs over this walk
 * there. The fallback before a dialog's first frame and the modal rule against the rows behind it
 * came from that suite, over maps built by hand; the rest is new.
 */
#include "framework/inkstand_test.h"

#include "inkstand/nav/dialog.h"

#include "inkcell/ui/focus.h"
#include "inkcell/ui/key.h"

#include <stdbool.h>
#include <stdint.h>

/* The ids a frame gave the two answers, and one it gave a row behind them. */
#define BASE 100U
#define ROW 7U

/* Accept on the right of Cancel, on one line - a wide panel's layout. */
static void side_by_side(struct inkcell_focus_map *map, struct inkcell_focus_item *items,
                         uint32_t capacity) {
    inkcell_focus_begin(map, items, capacity);
    (void)inkcell_focus_add(map, BASE + INKSTAND_DIALOG_CANCEL, 600, 480, 150, 44);
    (void)inkcell_focus_add(map, BASE + INKSTAND_DIALOG_ACCEPT, 770, 480, 230, 44);
}

/* Accept above Cancel - a narrow panel's layout. */
static void stacked(struct inkcell_focus_map *map, struct inkcell_focus_item *items,
                    uint32_t capacity) {
    inkcell_focus_begin(map, items, capacity);
    (void)inkcell_focus_add(map, BASE + INKSTAND_DIALOG_ACCEPT, 20, 300, 160, 44);
    (void)inkcell_focus_add(map, BASE + INKSTAND_DIALOG_CANCEL, 20, 352, 160, 44);
}

/* ---- the walk ------------------------------------------------------------------------------- */

INKSTAND_TEST_CASE(dialog_answer_follows_the_layout_it_was_drawn_in, unit) {
    struct inkcell_focus_item items[2];
    struct inkcell_focus_map map;

    side_by_side(&map, items, 2U);
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_answer(&map, BASE, INKCELL_KEY_RIGHT,
                                                 INKSTAND_DIALOG_CANCEL) != INKSTAND_DIALOG_ACCEPT,
                          "right from Cancel should reach Accept beside it");
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_answer(&map, BASE, INKCELL_KEY_LEFT,
                                                 INKSTAND_DIALOG_ACCEPT) != INKSTAND_DIALOG_CANCEL,
                          "left from Accept should reach Cancel beside it");
    /* The edge of the dialog, not a wrap: holding a key rests on the answer it reached. */
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_answer(&map, BASE, INKCELL_KEY_RIGHT,
                                                 INKSTAND_DIALOG_ACCEPT) != INKSTAND_DIALOG_ACCEPT,
                          "right from the rightmost answer should stay put rather than wrap");
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_answer(&map, BASE, INKCELL_KEY_DOWN,
                                                 INKSTAND_DIALOG_CANCEL) != INKSTAND_DIALOG_CANCEL,
                          "down should go nowhere when the answers share a line");

    stacked(&map, items, 2U);
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_answer(&map, BASE, INKCELL_KEY_DOWN,
                                                 INKSTAND_DIALOG_ACCEPT) != INKSTAND_DIALOG_CANCEL,
                          "down should reach the answer stacked under this one");
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_answer(&map, BASE, INKCELL_KEY_LEFT,
                                                 INKSTAND_DIALOG_ACCEPT) != INKSTAND_DIALOG_ACCEPT,
                          "left should go nowhere when the answers are stacked");
    record_success(test_name);
}

/* Before the dialog's first frame there is no map of it - none at all, or the frame under it. */
INKSTAND_TEST_CASE(dialog_answer_toggles_before_its_first_frame, unit) {
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_answer(NULL, BASE, INKCELL_KEY_RIGHT,
                                                 INKSTAND_DIALOG_CANCEL) != INKSTAND_DIALOG_ACCEPT,
                          "with no map, a direction should go to the other answer");

    /* A perfectly good map of the frame underneath, in which neither answer is yet. */
    struct inkcell_focus_item items[1];
    struct inkcell_focus_map map;
    inkcell_focus_begin(&map, items, 1U);
    (void)inkcell_focus_add(&map, ROW, 0, 0, 100, 40);
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_answer(&map, BASE, INKCELL_KEY_RIGHT,
                                                 INKSTAND_DIALOG_CANCEL) != INKSTAND_DIALOG_ACCEPT,
                          "a stale map should not trap the cursor on Cancel");
    record_success(test_name);
}

/* A row behind the dialog can sit nearer an answer than the other answer does. */
INKSTAND_TEST_CASE(dialog_answer_ignores_the_rows_behind_it, unit) {
    struct inkcell_focus_item items[3];
    struct inkcell_focus_map map;
    inkcell_focus_begin(&map, items, 3U);
    /* A full-width row spanning the buttons' vertical centre, which an unfiltered search from
       Cancel to the right would choose - and a modal cursor could then not land on. */
    (void)inkcell_focus_add(&map, ROW, 8, 493, 1000, 36);
    (void)inkcell_focus_add(&map, BASE + INKSTAND_DIALOG_CANCEL, 616, 481, 146, 44);
    (void)inkcell_focus_add(&map, BASE + INKSTAND_DIALOG_ACCEPT, 770, 481, 230, 44);
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_answer(&map, BASE, INKCELL_KEY_RIGHT,
                                                 INKSTAND_DIALOG_CANCEL) != INKSTAND_DIALOG_ACCEPT,
                          "the walk should see only the dialog's own answers");
    record_success(test_name);
}

/* ---- the dialog ----------------------------------------------------------------------------- */

INKSTAND_TEST_CASE(dialog_opens_on_cancel_and_closes_forgetting_its_subject, unit) {
    struct inkstand_dialog dialog = {0};
    inkstand_dialog_open(&dialog, 42U);
    INKSTAND_TEST_FAIL_IF(!dialog.open || dialog.cursor != INKSTAND_DIALOG_CANCEL ||
                              dialog.subject != 42U,
                          "a dialog should open on Cancel, about its subject");
    inkstand_dialog_close(&dialog);
    INKSTAND_TEST_FAIL_IF(dialog.open || dialog.subject != 0U,
                          "closing should take the dialog down and forget its subject");
    record_success(test_name);
}

INKSTAND_TEST_CASE(dialog_key_answers_and_closes, unit) {
    struct inkcell_focus_item items[2];
    struct inkcell_focus_map map;
    side_by_side(&map, items, 2U);
    struct inkstand_dialog dialog = {0};
    uint16_t subject = 0U;

    /* A repeated press on what raised it lands on Cancel, which changes nothing. */
    inkstand_dialog_open(&dialog, 7U);
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_key(&dialog, &map, BASE, INKCELL_KEY_A, &subject) !=
                                  INKSTAND_DIALOG_CANCELLED ||
                              dialog.open || subject != 7U,
                          "A on Cancel should cancel, close, and say what was cancelled");

    inkstand_dialog_open(&dialog, 8U);
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_key(&dialog, &map, BASE, INKCELL_KEY_RIGHT, &subject) !=
                              INKSTAND_DIALOG_MOVED,
                          "a direction with an answer that way should move");
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_key(&dialog, &map, BASE, INKCELL_KEY_RIGHT, &subject) !=
                              INKSTAND_DIALOG_IGNORED,
                          "a direction with nothing that way should be ignored");
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_key(&dialog, &map, BASE, INKCELL_KEY_SELECT, &subject) !=
                                  INKSTAND_DIALOG_IGNORED ||
                              !dialog.open,
                          "a key a dialog does not answer should leave it open");
    subject = 0U;
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_key(&dialog, &map, BASE, INKCELL_KEY_START, &subject) !=
                                  INKSTAND_DIALOG_ACCEPTED ||
                              dialog.open || subject != 8U,
                          "START on Accept should accept, close, and say what was accepted");

    /* B is never an answer to go ahead, wherever the cursor is. */
    inkstand_dialog_open(&dialog, 9U);
    dialog.cursor = INKSTAND_DIALOG_ACCEPT;
    INKSTAND_TEST_FAIL_IF(inkstand_dialog_key(&dialog, &map, BASE, INKCELL_KEY_B, &subject) !=
                                  INKSTAND_DIALOG_CANCELLED ||
                              dialog.open,
                          "B should cancel even with the cursor on Accept");

    INKSTAND_TEST_FAIL_IF(inkstand_dialog_key(&dialog, &map, BASE, INKCELL_KEY_A, &subject) !=
                              INKSTAND_DIALOG_IGNORED,
                          "a closed dialog should answer nothing");
    record_success(test_name);
}
