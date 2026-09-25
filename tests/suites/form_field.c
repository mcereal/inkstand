/*
 * The field descriptor: one row of a form, and the questions every caller asks of one.
 *
 * mesh-client's settings suite still holds what its two hundred rows *are* - which is a scale,
 * which bit a flag is, which section a row sits in - and runs over this descriptor there. These
 * are the cases for the descriptor itself, over a table of the suite's own that is built the way
 * an application builds one: the descriptor first in a larger row, read through a stride, with
 * row 0 standing for no field.
 */
#include "framework/inkstand_test.h"

#include "inkstand/form/field.h"

#include "inkcell/i18n/strings.h"
#include "inkcell/ui/anim.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define LEN(array) (sizeof(array) / sizeof((array)[0]))

/* ---- the table ------------------------------------------------------------------------------ */

enum { SECTION_NONE = 0, SECTION_A = 1, SECTION_B = 2, SECTION_EMPTY = 3 };

enum {
    F_NONE = 0,
    F_NAME,
    F_SHAPE,
    F_INTERVAL,
    F_PIN,
    F_FLAG_LOW,
    F_FLAG_HIGH,
    F_SECRET,
    F_ACTION,
    F_COUNT,
};

/* Answers NULL past its list, as an application's callback may for a value it does not know. */
static const char *shape_name(uint32_t value) {
    static const char *const k_names[] = {"circle", "square", "triangle"};
    return value < LEN(k_names) ? k_names[value] : NULL;
}

static const uint32_t k_intervals[] = {0U, 10U, 30U, 60U};
static const uint32_t k_pins[] = {0U, 1U, 2U, 3U};

/* A column after the descriptor, as an application's formatter or zero-word would be - here a
   whole word, so the row is larger than the descriptor and a stride that ignored it would read
   the wrong row. */
struct app_row {
    struct inkstand_form_field form;
    uint64_t app_column;
};

static const struct app_row k_rows[F_COUNT] = {
    [F_NONE] = {{0, INKSTAND_FORM_INFO, SECTION_NONE, 0U, NULL, {NULL, 0U, false, false}, 0U, 0},
                0xDEADU},
    [F_NAME] = {{1, INKSTAND_FORM_TEXT, SECTION_A, 39U, NULL, {NULL, 0U, false, false}, 0U, 0}, 1U},
    [F_SHAPE] =
        {{2, INKSTAND_FORM_ENUM, SECTION_A, 3U, shape_name, {NULL, 0U, false, false}, 0U, 7}, 2U},
    [F_INTERVAL] = {{3,
                     INKSTAND_FORM_NUMBER,
                     SECTION_A,
                     0U,
                     NULL,
                     {k_intervals, LEN(k_intervals), true, false},
                     0U,
                     0},
                    3U},
    [F_PIN] =
        {{4, INKSTAND_FORM_NUMBER, SECTION_B, 0U, NULL, {k_pins, LEN(k_pins), false, false}, 0U, 0},
         4U},
    [F_FLAG_LOW] =
        {{5, INKSTAND_FORM_FLAG, SECTION_B, 1U << 0, NULL, {NULL, 0U, false, false}, 0U, 0}, 5U},
    [F_FLAG_HIGH] =
        {{6, INKSTAND_FORM_FLAG, SECTION_B, 1U << 9, NULL, {NULL, 0U, false, false}, 0U, 0}, 6U},
    [F_SECRET] = {{7, INKSTAND_FORM_KEY, SECTION_B, 64U, NULL, {NULL, 0U, false, false}, 0x5U, 0},
                  7U},
    [F_ACTION] = {{8, INKSTAND_FORM_ACTION, SECTION_B, 0U, NULL, {NULL, 0U, false, false}, 0U, 0},
                  8U},
};

static const struct inkstand_form k_form = {k_rows, sizeof k_rows[0], F_COUNT};

/* ---- rows ----------------------------------------------------------------------------------- */

/* A stride larger than the descriptor is the whole point: every row is the application's row. */
INKSTAND_TEST_CASE(form_field_reads_each_row_through_the_stride, unit) {
    for (uint16_t id = 0U; id < F_COUNT; ++id) {
        const struct inkstand_form_field *field = inkstand_form_field(&k_form, id);
        INKSTAND_TEST_FAIL_IF(field != &k_rows[id].form,
                              "each id should be its own row of the application's table");
        INKSTAND_TEST_FAIL_IF(field->label != (inkcell_str_id)id,
                              "each row should carry its own label");
    }
    /* And the application's own column is still where the application left it. */
    const struct app_row *row = (const struct app_row *)inkstand_form_field(&k_form, F_SECRET);
    INKSTAND_TEST_FAIL_IF(row->app_column != 7U,
                          "a column after the descriptor should belong to the same row");
    record_success(test_name);
}

/* Row 0 answers for every id the table does not have. */
INKSTAND_TEST_CASE(form_field_resolves_an_unknown_id_to_row_zero, unit) {
    INKSTAND_TEST_FAIL_IF(inkstand_form_field(&k_form, F_COUNT) != &k_rows[F_NONE].form ||
                              inkstand_form_field(&k_form, UINT16_MAX) != &k_rows[F_NONE].form,
                          "an id past the table should be row 0, not a read past it");
    INKSTAND_TEST_FAIL_IF(inkstand_form_enum_count(&k_form, UINT16_MAX) != 0U ||
                              inkstand_form_text_max(&k_form, UINT16_MAX) != 0U ||
                              inkstand_form_bit(&k_form, UINT16_MAX) != 0U,
                          "an unknown id should answer as a row with no values");
    record_success(test_name);
}

/* ---- per-kind questions --------------------------------------------------------------------- */

/* Each question is about one kind, and every other kind answers it with nothing - including a
   row whose `limit` means something else. */
INKSTAND_TEST_CASE(form_field_answers_each_question_only_for_its_own_kind, unit) {
    INKSTAND_TEST_FAIL_IF(inkstand_form_enum_count(&k_form, F_SHAPE) != 3U ||
                              inkstand_form_enum_count(&k_form, F_NAME) != 0U ||
                              inkstand_form_enum_count(&k_form, F_FLAG_HIGH) != 0U,
                          "only an ENUM row has a count of values");

    INKSTAND_TEST_FAIL_IF(inkstand_form_bit(&k_form, F_FLAG_LOW) != (1U << 0) ||
                              inkstand_form_bit(&k_form, F_FLAG_HIGH) != (1U << 9),
                          "a FLAG row's limit is its bit");
    INKSTAND_TEST_FAIL_IF(inkstand_form_bit(&k_form, F_SHAPE) != 0U ||
                              inkstand_form_bit(&k_form, F_NAME) != 0U,
                          "a limit on any other kind is not a bit");

    INKSTAND_TEST_FAIL_IF(inkstand_form_text_max(&k_form, F_NAME) != 39U ||
                              inkstand_form_text_max(&k_form, F_SECRET) != 64U,
                          "TEXT and KEY rows should answer their byte cap, unclamped");
    INKSTAND_TEST_FAIL_IF(inkstand_form_text_max(&k_form, F_SHAPE) != 0U,
                          "an ENUM row's count is not a byte cap");

    INKSTAND_TEST_FAIL_IF(inkstand_form_key_choices(&k_form, F_SECRET) != 0x5U ||
                              inkstand_form_key_choices(&k_form, F_ACTION) != 0U,
                          "only a KEY row has choices to walk");
    record_success(test_name);
}

INKSTAND_TEST_CASE(form_field_names_enum_values_and_never_answers_null, unit) {
    INKSTAND_TEST_FAIL_IF(strcmp(inkstand_form_enum_name(&k_form, F_SHAPE, 1U), "square") != 0,
                          "an ENUM row should name its values");
    const char *unknown = inkcell_str(INKCELL_STR_COMMON_UNKNOWN_SHORT);
    INKSTAND_TEST_FAIL_IF(inkstand_form_enum_name(&k_form, F_NAME, 0U) == NULL ||
                              strcmp(inkstand_form_enum_name(&k_form, F_NAME, 0U), unknown) != 0,
                          "any other kind should answer the word for unknown, not NULL");
    INKSTAND_TEST_FAIL_IF(inkstand_form_enum_name(&k_form, F_SHAPE, 9U) == NULL ||
                              strcmp(inkstand_form_enum_name(&k_form, F_SHAPE, 9U), unknown) != 0,
                          "a value the callback has no name for should be unknown, not NULL");
    record_success(test_name);
}

/* The number questions are the scale's, asked of the row - and only of a NUMBER row. */
INKSTAND_TEST_CASE(form_field_steps_and_places_a_number_row_by_its_presets, unit) {
    INKSTAND_TEST_FAIL_IF(inkstand_form_number_step(&k_form, F_INTERVAL, 10U, +1) != 30U ||
                              inkstand_form_number_step(&k_form, F_PIN, 2U, -1) != 1U,
                          "a NUMBER row should step through its own presets");
    INKSTAND_TEST_FAIL_IF(inkstand_form_number_step(&k_form, F_SHAPE, 1U, +1) != 1U,
                          "any other kind should be left where it is");

    struct inkstand_form_track track;
    INKSTAND_TEST_FAIL_IF(!inkstand_form_number_track(&k_form, F_INTERVAL, 60U, &track) ||
                              track.position != INKCELL_ANIM_ONE,
                          "a NUMBER row whose presets are a scale should place a value");
    INKSTAND_TEST_FAIL_IF(inkstand_form_number_track(&k_form, F_PIN, 2U, &track),
                          "a NUMBER row whose presets name things is not a track");
    INKSTAND_TEST_FAIL_IF(inkstand_form_number_track(&k_form, F_SHAPE, 1U, &track),
                          "any other kind is not a track");
    record_success(test_name);
}

/* ---- sections ------------------------------------------------------------------------------- */

INKSTAND_TEST_CASE(form_field_finds_a_section_by_its_rows_and_not_by_row_zero, unit) {
    INKSTAND_TEST_FAIL_IF(!inkstand_form_section_has_fields(&k_form, SECTION_A) ||
                              !inkstand_form_section_has_fields(&k_form, SECTION_B),
                          "a section with rows should say so");
    INKSTAND_TEST_FAIL_IF(inkstand_form_section_has_fields(&k_form, SECTION_EMPTY),
                          "a section with no rows should say so");
    /* Row 0 sits in SECTION_NONE, and it is no field. */
    INKSTAND_TEST_FAIL_IF(inkstand_form_section_has_fields(&k_form, SECTION_NONE),
                          "row 0 should not make its section a section with fields");
    record_success(test_name);
}
