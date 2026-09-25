/*
 * The number scale and the choice set: how a form row walks its values.
 *
 * mesh-client's settings suite still holds which of its fields are scales, which stand a zero
 * aside and which name rather than measure - and runs over these walks there. These are the cases
 * for the walks themselves, over preset lists of their own: the step at both ends and between
 * presets, placement in stop space, what a track refuses, and the set that a mask describes.
 */
#include "framework/inkstand_test.h"

#include "inkstand/form/scale.h"

#include "inkcell/ui/anim.h"

#include <stdbool.h>
#include <stdint.h>

#define LEN(array) (sizeof(array) / sizeof((array)[0]))

/* Intervals in seconds, where 0 is "off" - a true bottom, on the scale. */
static const uint32_t k_intervals[] = {0U, 10U, 15U, 30U, 60U, 120U, 300U, 600U};
static const struct inkstand_form_presets k_interval_scale = {k_intervals, LEN(k_intervals), true,
                                                              false};

/* A leading 0 that is a word - "as much as there is" - and a scale after it. */
static const uint32_t k_power[] = {0U, 2U, 5U, 10U, 15U, 20U, 25U, 30U};
static const struct inkstand_form_presets k_power_scale = {k_power, LEN(k_power), true, true};

/* A scale that starts above zero, because whatever receives it refuses anything smaller. */
static const uint32_t k_hours[] = {3600U, 7200U, 14400U, 43200U, 86400U};
static const struct inkstand_form_presets k_hours_scale = {k_hours, LEN(k_hours), true, false};

/* Numbers that name something - pins, say. Steppable; not a length. */
static const uint32_t k_pins[] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
static const struct inkstand_form_presets k_pin_names = {k_pins, LEN(k_pins), false, false};

/* ---- the step ------------------------------------------------------------------------------- */

INKSTAND_TEST_CASE(form_presets_step_to_the_next_preset_and_stop_at_the_ends, unit) {
    INKSTAND_TEST_FAIL_IF(inkstand_form_presets_step(&k_interval_scale, 60U, +1) != 120U ||
                              inkstand_form_presets_step(&k_interval_scale, 60U, -1) != 30U,
                          "a preset should step to its neighbours");
    INKSTAND_TEST_FAIL_IF(inkstand_form_presets_step(&k_interval_scale, 600U, +1) != 600U ||
                              inkstand_form_presets_step(&k_interval_scale, 0U, -1) != 0U,
                          "the ends should stay where they are rather than wrap");

    /* A value the list does not hold steps to the nearer preset in the direction asked. */
    INKSTAND_TEST_FAIL_IF(inkstand_form_presets_step(&k_interval_scale, 45U, +1) != 60U ||
                              inkstand_form_presets_step(&k_interval_scale, 45U, -1) != 30U,
                          "a value between presets should step onto the list");
    INKSTAND_TEST_FAIL_IF(inkstand_form_presets_step(&k_interval_scale, 9999U, -1) != 600U,
                          "a value past the top should step down onto the list");

    /* Naming or measuring makes no difference to a step. */
    INKSTAND_TEST_FAIL_IF(inkstand_form_presets_step(&k_pin_names, 3U, +1) != 4U,
                          "named presets should step like any others");
    INKSTAND_TEST_FAIL_IF(inkstand_form_presets_step(&k_interval_scale, 60U, 0) != 60U ||
                              inkstand_form_presets_step(NULL, 60U, +1) != 60U,
                          "no delta, or no presets, should leave the value alone");
    record_success(test_name);
}

/* An all-negative list cast to uint32_t keeps its order, and steps correctly unknowing. */
INKSTAND_TEST_CASE(form_presets_step_through_an_all_negative_list, unit) {
#define NEG(v) ((uint32_t)(int32_t)(v))
    static const uint32_t k_db[] = {NEG(-100), NEG(-90), NEG(-80), NEG(-70), NEG(-60)};
    static const struct inkstand_form_presets db = {k_db, LEN(k_db), true, false};

    INKSTAND_TEST_FAIL_IF(inkstand_form_presets_step(&db, NEG(-80), +1) != NEG(-70) ||
                              inkstand_form_presets_step(&db, NEG(-80), -1) != NEG(-90),
                          "a negative list should step in its own order");
    INKSTAND_TEST_FAIL_IF(inkstand_form_presets_step(&db, NEG(-60), +1) != NEG(-60),
                          "the top of a negative list should be its end");

    struct inkstand_form_track track;
    INKSTAND_TEST_FAIL_IF(!inkstand_form_presets_track(&db, NEG(-80), &track) || track.unplaced ||
                              track.position != INKCELL_ANIM_ONE / 2,
                          "the middle of five negative stops should be half way along");
#undef NEG
    record_success(test_name);
}

/* ---- the track ------------------------------------------------------------------------------ */

/*
 * The stops are evenly spaced, so a list that climbs geometrically is placed by index rather
 * than by value - and a value the list does not contain lands between the two stops it falls
 * between rather than being refused.
 */
INKSTAND_TEST_CASE(form_presets_track_places_in_stop_space, unit) {
    struct inkstand_form_track track;

    INKSTAND_TEST_FAIL_IF(!inkstand_form_presets_track(&k_interval_scale, 0U, &track),
                          "a scale should place a value");
    INKSTAND_TEST_FAIL_IF(track.unplaced || track.position != 0 || track.stops != 8U,
                          "a bottom that is on the scale should sit at the start, of eight stops");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_presets_track(&k_interval_scale, 600U, &track) ||
                              track.position != INKCELL_ANIM_ONE,
                          "the last preset should sit at the end of the track");

    /* The fourth of eight stops: three sevenths along, not 30/600 of the way. A value-space
       placement would answer 50, which is what makes this the assertion worth writing. */
    INKSTAND_TEST_FAIL_IF(!inkstand_form_presets_track(&k_interval_scale, 30U, &track) ||
                              track.position != 3 * INKCELL_ANIM_ONE / 7,
                          "the fourth of eight stops should be three sevenths in");

    /* 45 is half way from 30 to 60, so half a stop past the fourth. */
    INKSTAND_TEST_FAIL_IF(!inkstand_form_presets_track(&k_interval_scale, 45U, &track) ||
                              track.unplaced ||
                              track.position != (3 * INKCELL_ANIM_ONE + INKCELL_ANIM_ONE / 2) / 7,
                          "a value between two stops should belong between them");

    /* Past the top is at the end, not off it. */
    INKSTAND_TEST_FAIL_IF(!inkstand_form_presets_track(&k_interval_scale, 9999U, &track) ||
                              track.unplaced || track.position != INKCELL_ANIM_ONE,
                          "a value past the last stop should sit at the end");
    record_success(test_name);
}

/*
 * A value below the bottom stop is off the track, not at the bottom of it - whether the list
 * stands its first value aside as a word or simply starts above zero.
 */
INKSTAND_TEST_CASE(form_presets_track_refuses_what_it_cannot_place, unit) {
    struct inkstand_form_track track;

    INKSTAND_TEST_FAIL_IF(!inkstand_form_presets_track(&k_power_scale, 0U, &track),
                          "a scale with a word aside is still a scale; the row draws one");
    INKSTAND_TEST_FAIL_IF(!track.unplaced || track.position != 0,
                          "a word stood aside is not a point on the scale");
    INKSTAND_TEST_FAIL_IF(track.stops != 7U,
                          "the track should count the stops after the word, not the word");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_presets_track(&k_power_scale, 2U, &track) ||
                              track.unplaced || track.position != 0,
                          "the first value after the word should be the bottom of the track");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_presets_track(&k_power_scale, 30U, &track) ||
                              track.position != INKCELL_ANIM_ONE,
                          "the top should be the end of the track");

    INKSTAND_TEST_FAIL_IF(!inkstand_form_presets_track(&k_hours_scale, 0U, &track) ||
                              !track.unplaced || track.stops != 5U,
                          "0 under a list that starts above zero should be off the track");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_presets_track(&k_hours_scale, 3600U, &track) ||
                              track.unplaced || track.position != 0,
                          "the list's own bottom should be on it");

    /* Names are not magnitudes, and a list too short to be a line is not a track. */
    INKSTAND_TEST_FAIL_IF(inkstand_form_presets_track(&k_pin_names, 3U, &track),
                          "named presets must not be drawn as a length");
    static const uint32_t k_one[] = {0U, 5U};
    static const struct inkstand_form_presets one_stop = {k_one, LEN(k_one), true, true};
    INKSTAND_TEST_FAIL_IF(inkstand_form_presets_track(&one_stop, 5U, &track),
                          "a single stop after the word is not a track");
    INKSTAND_TEST_FAIL_IF(inkstand_form_presets_track(NULL, 5U, &track), "no presets is no track");
    record_success(test_name);
}

/* A repeat would make a zero span; the stop itself is the answer, not a division by zero. */
INKSTAND_TEST_CASE(form_presets_track_survives_a_repeated_stop, unit) {
    static const uint32_t k_repeat[] = {10U, 20U, 20U, 40U};
    static const struct inkstand_form_presets repeat = {k_repeat, LEN(k_repeat), true, false};
    struct inkstand_form_track track;

    INKSTAND_TEST_FAIL_IF(!inkstand_form_presets_track(&repeat, 20U, &track) || track.unplaced,
                          "a value on a repeated stop should still place");
    INKSTAND_TEST_FAIL_IF(track.position < 0 || track.position > INKCELL_ANIM_ONE,
                          "the position should stay on the track");
    record_success(test_name);
}

/* ---- the choice set ------------------------------------------------------------------------- */

INKSTAND_TEST_CASE(form_choice_step_walks_the_set, unit) {
    /* No mask is no constraint: the plain wrap-around. */
    INKSTAND_TEST_FAIL_IF(inkstand_form_choice_step(0U, 4U, 3U, +1) != 0U,
                          "an unconstrained row should wrap past the last value");
    INKSTAND_TEST_FAIL_IF(inkstand_form_choice_step(0U, 4U, 0U, -1) != 3U,
                          "an unconstrained row should wrap past the first value");

    /* Values 0, 3 and 5 legal out of eight. Both directions skip the rest. */
    const uint32_t mask = (1U << 0) | (1U << 3) | (1U << 5);
    INKSTAND_TEST_FAIL_IF(inkstand_form_choice_step(mask, 8U, 0U, +1) != 3U,
                          "Right should land on the next legal value, not the next value");
    INKSTAND_TEST_FAIL_IF(inkstand_form_choice_step(mask, 8U, 3U, +1) != 5U,
                          "Right should keep skipping what the set leaves out");
    INKSTAND_TEST_FAIL_IF(inkstand_form_choice_step(mask, 8U, 5U, +1) != 0U,
                          "Right off the end of the set should wrap to its first value");
    INKSTAND_TEST_FAIL_IF(inkstand_form_choice_step(mask, 8U, 0U, -1) != 5U,
                          "Left off the start of the set should wrap to its last value");

    /* A value the set leaves out is still steppable: the other end may be holding one, and the
       row has to be able to get off it. */
    INKSTAND_TEST_FAIL_IF(inkstand_form_choice_step(mask, 8U, 4U, +1) != 5U,
                          "a row sitting on an illegal value should step to a legal one");
    INKSTAND_TEST_FAIL_IF(inkstand_form_choice_step(0U, 4U, 9U, +1) != 1U,
                          "a value past the range should walk from inside it");

    /* One legal value, and none: the press does nothing rather than pretending to. */
    INKSTAND_TEST_FAIL_IF(inkstand_form_choice_step(1U << 2, 8U, 2U, +1) != 2U,
                          "a set of one should leave the row where it is");
    INKSTAND_TEST_FAIL_IF(inkstand_form_choice_step(1U << 9, 8U, 1U, +1) != 1U,
                          "a set with nothing inside the range should leave the row where it is");
    INKSTAND_TEST_FAIL_IF(inkstand_form_choice_step(0U, 0U, 1U, +1) != 1U,
                          "an empty range should leave the row where it is");
    record_success(test_name);
}

INKSTAND_TEST_CASE(form_choice_allowed_reads_the_set_and_the_range, unit) {
    const uint32_t mask = (1U << 0) | (1U << 3) | (1U << 5);

    INKSTAND_TEST_FAIL_IF(!inkstand_form_choice_allowed(0U, 4U, 3U),
                          "every value in range is allowed when nothing constrains the row");
    INKSTAND_TEST_FAIL_IF(inkstand_form_choice_allowed(0U, 4U, 4U),
                          "a value past the end of the range is not allowed by an empty mask");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_choice_allowed(mask, 8U, 3U) ||
                              inkstand_form_choice_allowed(mask, 8U, 4U),
                          "the predicate should read the set it is given");
    /* A stale mask cannot offer a value the row no longer has. */
    INKSTAND_TEST_FAIL_IF(inkstand_form_choice_allowed(mask, 4U, 5U),
                          "a bit past the range should not count");
    /* And a range wider than the word has no bit to test past 31. */
    INKSTAND_TEST_FAIL_IF(inkstand_form_choice_allowed(mask, 64U, 40U) ||
                              !inkstand_form_choice_allowed(0U, 64U, 40U),
                          "past the word, only an unconstrained row allows a value");
    record_success(test_name);
}
