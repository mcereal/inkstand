/*
 * The snackbar's queue: one notice showing, how long it stands, and what waits behind it.
 *
 * mesh-client's nav suites still hold where its notices come from - which press sets one, which
 * arrival posts one, that a key press on the store dates what it raised - and run over this queue
 * there. These are the cases for the queue itself: the queue case came from mesh-client whole, the
 * expiry and dismiss rules were sliced out of its navigation case, and the undated path is new.
 */
#include "framework/inkstand_test.h"

#include "inkstand/nav/toast.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* A second notice waits rather than overwriting the first, the tick that retires one promotes the
   next, a repeat is dropped, and a full queue loses its oldest waiting entry. */
INKSTAND_TEST_CASE(toast_queues_rather_than_overwrites, unit) {
    struct inkstand_toast toast;
    inkstand_toast_init(&toast);

    inkstand_toast_post(&toast, 1000U, "first");
    inkstand_toast_post(&toast, 1000U, "second");
    INKSTAND_TEST_FAIL_IF(strcmp(toast.text, "first") != 0 || toast.queued != 1U,
                          "a notice arriving while one is up should wait behind it");

    /* One event reported twice in a row is one notice, not two. */
    inkstand_toast_post(&toast, 1000U, "second");
    INKSTAND_TEST_FAIL_IF(toast.queued != 1U,
                          "a repeat of the newest waiting notice should be dropped");

    /* A press does not wait: it replaces what is showing and leaves what is waiting alone. */
    inkstand_toast_set(&toast, 1200U, "a press answered");
    INKSTAND_TEST_FAIL_IF(strcmp(toast.text, "a press answered") != 0 || toast.queued != 1U,
                          "a press should take the snackbar without discarding what was waiting");
    inkstand_toast_set(&toast, 1200U, "first");

    /* Nothing moves until the showing notice has stood its time. */
    INKSTAND_TEST_FAIL_IF(inkstand_toast_tick(&toast, 2000U) || strcmp(toast.text, "first") != 0,
                          "a notice should not be cut short by the one waiting behind it");
    INKSTAND_TEST_FAIL_IF(!inkstand_toast_tick(&toast, 1200U + INKSTAND_TOAST_STAND_MS) ||
                              strcmp(toast.text, "second") != 0,
                          "the tick that retires a notice should promote the next");
    /* Dated from the promotion: a deadline the backend has not seen is how it tells one notice
       from the next. */
    INKSTAND_TEST_FAIL_IF(toast.until_ms != 1200U + 2U * INKSTAND_TOAST_STAND_MS ||
                              toast.queued != 0U,
                          "a promoted notice should start its own time from the promotion");
    INKSTAND_TEST_FAIL_IF(!inkstand_toast_tick(&toast, 20000U) || toast.text[0] != '\0',
                          "an empty queue should let the snackbar go");

    /* A burst longer than the queue keeps the newest - a notice is only worth showing while it
       is still news. */
    inkstand_toast_post(&toast, 30000U, "showing");
    inkstand_toast_post(&toast, 30000U, "a");
    inkstand_toast_post(&toast, 30000U, "b");
    inkstand_toast_post(&toast, 30000U, "c");
    inkstand_toast_post(&toast, 30000U, "d");
    INKSTAND_TEST_FAIL_IF(toast.queued != INKSTAND_TOAST_QUEUE ||
                              strcmp(toast.queue[0], "b") != 0 ||
                              strcmp(toast.queue[INKSTAND_TOAST_QUEUE - 1U], "d") != 0,
                          "a full queue should drop its oldest waiting notice, not its newest");

    /* Clearing clears the backlog, or the program starts talking again a moment later. */
    inkstand_toast_set(&toast, 30000U, NULL);
    INKSTAND_TEST_FAIL_IF(toast.text[0] != '\0' || toast.queued != 0U,
                          "clearing the notice should clear what was waiting behind it");
    record_success(test_name);
}

/* A notice stands its time, and a repeat of what is showing - not only of what waits - is one. */
INKSTAND_TEST_CASE(toast_stands_its_time_and_drops_a_repeat_of_what_shows, unit) {
    struct inkstand_toast toast;
    inkstand_toast_init(&toast);

    inkstand_toast_set(&toast, 1000U, "saved");
    INKSTAND_TEST_FAIL_IF(toast.until_ms != 1000U + INKSTAND_TOAST_STAND_MS,
                          "a set notice should stand from the clock it was given");
    INKSTAND_TEST_FAIL_IF(inkstand_toast_tick(&toast, 1000U + INKSTAND_TOAST_STAND_MS - 1U),
                          "a notice should not go a millisecond early");
    inkstand_toast_post(&toast, 1500U, "saved");
    INKSTAND_TEST_FAIL_IF(toast.queued != 0U,
                          "an arrival repeating what is showing should not queue");
    INKSTAND_TEST_FAIL_IF(!inkstand_toast_tick(&toast, 1000U + INKSTAND_TOAST_STAND_MS) ||
                              toast.text[0] != '\0',
                          "a notice should go when its time is up");
    INKSTAND_TEST_FAIL_IF(inkstand_toast_tick(&toast, 99999U),
                          "a tick with nothing showing should change nothing");

    /* An empty post says nothing, and a notice too long for the snackbar is cut to fit. */
    inkstand_toast_post(&toast, 2000U, "");
    INKSTAND_TEST_FAIL_IF(toast.text[0] != '\0', "an empty arrival should raise nothing");
    char longer[INKSTAND_TOAST_TEXT_MAX * 2U];
    memset(longer, 'x', sizeof longer - 1U);
    longer[sizeof longer - 1U] = '\0';
    inkstand_toast_set(&toast, 2000U, longer);
    INKSTAND_TEST_FAIL_IF(strlen(toast.text) != INKSTAND_TOAST_TEXT_MAX - 1U,
                          "a notice too long for the snackbar should be cut to fit");
    /* And a repeat of it is still a repeat, cut or not - against what is showing and against the
       newest thing waiting. */
    inkstand_toast_post(&toast, 2000U, longer);
    INKSTAND_TEST_FAIL_IF(toast.queued != 0U,
                          "a long notice repeating what is showing should not queue");
    inkstand_toast_post(&toast, 2000U, "between");
    longer[0] = 'y';
    inkstand_toast_post(&toast, 2000U, longer);
    inkstand_toast_post(&toast, 2000U, longer);
    INKSTAND_TEST_FAIL_IF(toast.queued != 2U,
                          "a long notice repeating the newest waiting one should be dropped");
    record_success(test_name);
}

/*
 * A press dismisses what is showing, and the next one waiting takes the snackbar once the press is
 * done: date() puts it up from the driver's clock, and until then it is still queued.
 */
INKSTAND_TEST_CASE(toast_dismiss_hands_the_snackbar_to_the_next_one_waiting, unit) {
    struct inkstand_toast toast;
    inkstand_toast_init(&toast);

    INKSTAND_TEST_FAIL_IF(inkstand_toast_dismiss(&toast),
                          "dismissing nothing should report nothing dismissed");

    inkstand_toast_post(&toast, 1000U, "first");
    inkstand_toast_post(&toast, 1000U, "second");
    inkstand_toast_post(&toast, 1000U, "third");
    INKSTAND_TEST_FAIL_IF(!inkstand_toast_dismiss(&toast) || toast.text[0] != '\0' ||
                              toast.queued != 2U,
                          "a dismiss should take down only what was showing");
    inkstand_toast_date(&toast, 1200U);
    INKSTAND_TEST_FAIL_IF(strcmp(toast.text, "second") != 0 ||
                              toast.until_ms != 1200U + INKSTAND_TOAST_STAND_MS ||
                              toast.queued != 1U,
                          "after the press, the oldest waiting notice should go up, dated");

    /* A later arrival waits behind it rather than jumping the queue. */
    inkstand_toast_post(&toast, 1300U, "fourth");
    INKSTAND_TEST_FAIL_IF(toast.queued != 2U || strcmp(toast.queue[0], "third") != 0,
                          "an arrival after a dismiss should wait behind what was uncovered");

    (void)inkstand_toast_dismiss(&toast);
    inkstand_toast_date(&toast, 1400U);
    (void)inkstand_toast_dismiss(&toast);
    inkstand_toast_date(&toast, 1500U);
    INKSTAND_TEST_FAIL_IF(strcmp(toast.text, "fourth") != 0 || toast.queued != 0U,
                          "each dismiss should uncover the next, in order");
    INKSTAND_TEST_FAIL_IF(!inkstand_toast_dismiss(&toast) || toast.text[0] != '\0',
                          "dismissing the last one should leave the snackbar empty");
    inkstand_toast_date(&toast, 1600U);
    INKSTAND_TEST_FAIL_IF(toast.text[0] != '\0',
                          "dating an empty snackbar with nothing waiting should put up nothing");
    record_success(test_name);
}

/*
 * The press that dismisses a notice usually answers with one of its own. The answer takes the
 * snackbar and what was waiting keeps waiting - promoted during the press, it would have been put
 * up and overwritten in the same breath, and lost.
 */
INKSTAND_TEST_CASE(toast_a_press_that_answers_keeps_what_was_waiting, unit) {
    struct inkstand_toast toast;
    inkstand_toast_init(&toast);

    inkstand_toast_post(&toast, 1000U, "arrived");
    inkstand_toast_post(&toast, 1000U, "arrived too");
    (void)inkstand_toast_dismiss(&toast);
    inkstand_toast_raise(&toast, "the press answered");
    inkstand_toast_date(&toast, 1200U);
    INKSTAND_TEST_FAIL_IF(strcmp(toast.text, "the press answered") != 0 || toast.queued != 1U ||
                              strcmp(toast.queue[0], "arrived too") != 0,
                          "a press's own notice should show and leave the waiting one queued");
    INKSTAND_TEST_FAIL_IF(!inkstand_toast_tick(&toast, 1200U + INKSTAND_TOAST_STAND_MS) ||
                              strcmp(toast.text, "arrived too") != 0,
                          "the waiting notice should follow once the answer has stood");

    /* The same with a notice set from a clock rather than raised. */
    inkstand_toast_post(&toast, 9000U, "later");
    (void)inkstand_toast_dismiss(&toast);
    inkstand_toast_set(&toast, 9100U, "set by the press");
    INKSTAND_TEST_FAIL_IF(strcmp(toast.text, "set by the press") != 0 || toast.queued != 1U,
                          "a set notice after a dismiss should leave the waiting one queued");
    record_success(test_name);
}

/* Nothing showing is not nothing to show: a tick puts up what a dismiss left waiting. */
INKSTAND_TEST_CASE(toast_tick_puts_up_what_was_left_waiting, unit) {
    struct inkstand_toast toast;
    inkstand_toast_init(&toast);

    inkstand_toast_post(&toast, 1000U, "first");
    inkstand_toast_post(&toast, 1000U, "second");
    (void)inkstand_toast_dismiss(&toast);
    INKSTAND_TEST_FAIL_IF(!inkstand_toast_tick(&toast, 1100U) ||
                              strcmp(toast.text, "second") != 0 ||
                              toast.until_ms != 1100U + INKSTAND_TOAST_STAND_MS,
                          "a tick should put up a waiting notice when nothing is showing");
    INKSTAND_TEST_FAIL_IF(inkstand_toast_tick(&toast, 1200U),
                          "and then leave it standing its time");
    record_success(test_name);
}

/* A press has no clock: it raises undated, and the driver's clock dates it. */
INKSTAND_TEST_CASE(toast_raise_is_undated_until_the_driver_dates_it, unit) {
    struct inkstand_toast toast;
    inkstand_toast_init(&toast);

    inkstand_toast_raise(&toast, "refused");
    INKSTAND_TEST_FAIL_IF(strcmp(toast.text, "refused") != 0 || toast.until_ms != 0U,
                          "a raised notice should show undated");
    inkstand_toast_date(&toast, 5000U);
    INKSTAND_TEST_FAIL_IF(toast.until_ms != 5000U + INKSTAND_TOAST_STAND_MS,
                          "dating should give a raised notice its time from that clock");
    /* Dating twice is dating once: the second clock is not the one it started standing by. */
    inkstand_toast_date(&toast, 9000U);
    INKSTAND_TEST_FAIL_IF(toast.until_ms != 5000U + INKSTAND_TOAST_STAND_MS,
                          "a dated notice should keep its deadline");

    /* The contract a driver keeps: date before the next tick, because a tick retires an undated
       notice at once - its deadline of 0 has already passed. */
    inkstand_toast_raise(&toast, "again");
    INKSTAND_TEST_FAIL_IF(!inkstand_toast_tick(&toast, 1U) || toast.text[0] != '\0',
                          "a tick should retire a notice nobody dated");
    record_success(test_name);
}
