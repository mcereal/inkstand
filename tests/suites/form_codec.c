/*
 * The form codec: decimals, identifiers and bytes, as text and back.
 *
 * The first application's settings suite still holds its own spellings - the places it holds
 * a position to, the marker its identifiers carry, the three sizes a key is read as hex at - and
 * runs over this codec there. These are the cases for the half that came down, sliced from that
 * suite and made to name no application: the rounding, the refusals, the ambiguous identifier, the
 * padding. The cases for what became an argument - the marker, the hex sizes - are new.
 */
#include "framework/inkstand_test.h"

#include "inkstand/form/codec.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ---- decimals ------------------------------------------------------------------------------- */

/*
 * The same parse and print at different places. What the codec's generality makes possible is a
 * *different* number of places, and the bug it would hide is a scale right at seven and wrong at
 * four - so both directions, at more than one width.
 */
INKSTAND_TEST_CASE(form_decimal_round_trips_at_every_width, unit) {
    char text[32];
    int64_t value = 0;

    /* Four places held and four shown. */
    inkstand_form_decimal_text(9068750, 4U, 4U, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "906.8750") != 0, "four places print wrong");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_decimal_parse("906.875", 4U, 3000, &value) ||
                              value != 9068750,
                          "fewer places typed than held should be padded");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_decimal_parse(text, 4U, 3000, &value) || value != 9068750,
                          "a printed decimal should parse back to itself");

    /* No places at all: a whole number, printed with no point. */
    INKSTAND_TEST_FAIL_IF(!inkstand_form_decimal_parse("42", 0U, 65535, &value) || value != 42,
                          "a whole number should parse at no places");
    inkstand_form_decimal_text(42, 0U, 0U, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "42") != 0, "a whole number should print with no point");

    /* Signed, at one place. */
    INKSTAND_TEST_FAIL_IF(!inkstand_form_decimal_parse("-12.5", 1U, 1000000, &value) ||
                              value != -125,
                          "a negative decimal should parse");
    inkstand_form_decimal_text(-125, 1U, 1U, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "-12.5") != 0, "a negative decimal prints wrong");

    /* Seven held, round trip through the printed form. */
    inkstand_form_decimal_text(-635752000, 7U, 7U, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(!inkstand_form_decimal_parse(text, 7U, 180, &value) ||
                              value != -635752000,
                          "seven places should survive the trip");
    record_success(test_name);
}

/* Narrower than held is rounded, and the sign survives a zero whole part. */
INKSTAND_TEST_CASE(form_decimal_prints_rounded_and_keeps_a_small_sign, unit) {
    char text[32];

    inkstand_form_decimal_text(445999999, 7U, 5U, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "44.60000") != 0,
                          "a narrowed decimal should round, not cut");
    inkstand_form_decimal_text(446488000, 7U, 5U, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "44.64880") != 0,
                          "a positive narrowed decimal prints wrong");

    /* Between -1 and 0 the whole part is zero, so the sign has nowhere else to live. */
    inkstand_form_decimal_text(-5000000, 7U, 5U, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "-0.50000") != 0,
                          "a value inside the first unit should keep its sign");

    /* Shown wider than held is shown at held. */
    inkstand_form_decimal_text(125, 1U, 4U, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "12.5") != 0, "shown wider than held should clamp to held");
    record_success(test_name);
}

/* The ends of int64_t, where a negation or a rounding step used to leave the type. */
INKSTAND_TEST_CASE(form_decimal_prints_the_whole_range, unit) {
    char text[32];

    inkstand_form_decimal_text(INT64_MIN, 0U, 0U, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "-9223372036854775808") != 0,
                          "INT64_MIN has no int64_t magnitude, and should still print");
    inkstand_form_decimal_text(INT64_MIN, 9U, 2U, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "-9223372036.85") != 0,
                          "INT64_MIN narrowed should round its magnitude like any other");

    /* 854775807 dropped rounds the last shown place up - the add that used to overflow. */
    inkstand_form_decimal_text(INT64_MAX, 9U, 0U, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "9223372037") != 0,
                          "INT64_MAX narrowed should round up without leaving the type");
    inkstand_form_decimal_text(INT64_MAX, 2U, 2U, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "92233720368547758.07") != 0,
                          "INT64_MAX at its own width should print every digit");
    record_success(test_name);
}

/* A range that cannot be held once scaled is refused, not multiplied. */
INKSTAND_TEST_CASE(form_decimal_refuses_a_limit_too_large_to_scale, unit) {
    int64_t value = 0;

    INKSTAND_TEST_FAIL_IF(
        inkstand_form_decimal_parse("0", 9U, INT64_MAX, &value) ||
            inkstand_form_decimal_parse("1", 1U, INT64_MAX / 10, &value),
        "a limit that cannot be scaled should be refused, even for a small value");
    const int64_t largest = INT64_MAX / 1000000000 - 1;
    INKSTAND_TEST_FAIL_IF(!inkstand_form_decimal_parse("-9223372035", 9U, largest, &value) ||
                              value != -9223372035000000000,
                          "the largest limit that fits should still take the limit itself");
    INKSTAND_TEST_FAIL_IF(inkstand_form_decimal_parse("9223372035.000000001", 9U, largest, &value),
                          "a fraction past the limit should be refused, not overflow");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_decimal_parse("-42", 0U, INT64_MAX - 1, &value) ||
                              value != -42,
                          "with no places the limit may be almost the whole type");
    record_success(test_name);
}

/* What keeps a typo from becoming a value. */
INKSTAND_TEST_CASE(form_decimal_refuses_rather_than_guesses, unit) {
    int64_t value = 0;

    /* A bare point, signed or not, would otherwise read as zero - a real value nobody typed. */
    INKSTAND_TEST_FAIL_IF(inkstand_form_decimal_parse(".", 7U, 90, &value) ||
                              inkstand_form_decimal_parse("-.", 7U, 90, &value) ||
                              inkstand_form_decimal_parse("+.", 7U, 90, &value) ||
                              inkstand_form_decimal_parse("", 7U, 90, &value) ||
                              inkstand_form_decimal_parse("   ", 7U, 90, &value),
                          "an empty row or a bare point should be refused");
    INKSTAND_TEST_FAIL_IF(inkstand_form_decimal_parse("44.6N", 7U, 90, &value) ||
                              inkstand_form_decimal_parse("north", 7U, 90, &value) ||
                              inkstand_form_decimal_parse("906.8 MHz", 4U, 3000, &value),
                          "trailing rubbish should be refused");
    INKSTAND_TEST_FAIL_IF(inkstand_form_decimal_parse("91", 7U, 90, &value) ||
                              inkstand_form_decimal_parse("-90.5", 7U, 90, &value) ||
                              inkstand_form_decimal_parse("4000", 4U, 3000, &value),
                          "a magnitude past the limit should be refused, either sign");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_decimal_parse("90", 7U, 90, &value) || value != 900000000,
                          "the limit itself should be accepted");

    /* Finer than held: refused when the digit means something, allowed when it is a zero. */
    INKSTAND_TEST_FAIL_IF(inkstand_form_decimal_parse("12.5", 0U, 100, &value),
                          "a whole-number row should not take 12.5 as 12");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_decimal_parse("12.50", 1U, 100, &value) || value != 125,
                          "trailing zeros past the held places should be allowed through");

    INKSTAND_TEST_FAIL_IF(
        inkstand_form_decimal_parse("1", INKSTAND_FORM_DECIMAL_DIGITS_MAX + 1U, 10, &value) ||
            inkstand_form_decimal_parse("1", 2U, 0, &value) ||
            inkstand_form_decimal_parse(NULL, 2U, 10, &value),
        "places past the maximum, a limit of nothing and no text should be refused");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_decimal_parse("  7 ", 2U, 10, &value) || value != 700,
                          "surrounding spaces should be allowed");
    record_success(test_name);
}

/* ---- identifiers ---------------------------------------------------------------------------- */

INKSTAND_TEST_CASE(form_id_prints_with_its_marker_and_zero_as_empty, unit) {
    char text[16];

    inkstand_form_id_text(0x433D1B2CU, '!', text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "!433d1b2c") != 0, "an identifier prints wrong");
    inkstand_form_id_text(0x1AU, '#', text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "#0000001a") != 0,
                          "the marker should be the caller's, and the width fixed");
    inkstand_form_id_text(0U, '!', text, sizeof text);
    INKSTAND_TEST_FAIL_IF(text[0] != '\0', "zero should print as an empty row");
    record_success(test_name);
}

/* In whichever of the spellings somebody has in front of them. */
INKSTAND_TEST_CASE(form_id_parses_every_spelling, unit) {
    uint32_t id = 0U;

    INKSTAND_TEST_FAIL_IF(!inkstand_form_id_parse("!433d1b2c", '!', &id) || id != 0x433D1B2CU,
                          "the marked spelling should parse");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_id_parse("433D1B2C", '!', &id) || id != 0x433D1B2CU,
                          "bare hex with a letter in it should parse, upper case included");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_id_parse("0x433d1b2c", '!', &id) || id != 0x433D1B2CU,
                          "a 0x prefix should parse");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_id_parse("123456", '!', &id) || id != 123456U,
                          "a plain decimal should parse");

    /*
     * Eight digits and no letters is the ambiguous case, and it reads as decimal: a letter means
     * hex, all digits mean decimal, and the marker is always there to ask for hex. Guessing on
     * the width made eight digits mean something seven and nine did not.
     */
    INKSTAND_TEST_FAIL_IF(!inkstand_form_id_parse("12345678", '!', &id) || id != 12345678U,
                          "eight bare digits are decimal, because nothing in them says hex");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_id_parse("!12345678", '!', &id) || id != 0x12345678U,
                          "and the marker is how the same digits are asked for as hex");

    /* The marker is the caller's: another program's is honoured, and this one's is then not. */
    INKSTAND_TEST_FAIL_IF(!inkstand_form_id_parse("#1a", '#', &id) || id != 0x1AU,
                          "a caller's own marker should mean hex");
    INKSTAND_TEST_FAIL_IF(inkstand_form_id_parse("!1a", '#', &id),
                          "a marker that is not the caller's is rubbish");
    record_success(test_name);
}

INKSTAND_TEST_CASE(form_id_reads_an_empty_row_as_zero_and_refuses_the_rest, unit) {
    uint32_t id = 7U;

    /* An empty slot is not a bad one: a parse that refused it would make the row unclearable. */
    INKSTAND_TEST_FAIL_IF(!inkstand_form_id_parse("  ", '!', &id) || id != 0U,
                          "an empty row should be zero rather than a bad value");
    INKSTAND_TEST_FAIL_IF(inkstand_form_id_parse("!", '!', &id) ||
                              inkstand_form_id_parse("nope", '!', &id) ||
                              inkstand_form_id_parse("99999999999", '!', &id) ||
                              inkstand_form_id_parse("!100000000", '!', &id),
                          "a bare marker, a word and anything past 32 bits should be refused");
    record_success(test_name);
}

/* ---- bytes ---------------------------------------------------------------------------------- */

static const size_t k_sizes[] = {1U, 16U, 32U};
#define K_SIZE_COUNT (sizeof k_sizes / sizeof k_sizes[0])

static const uint8_t k_sixteen[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                      0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

INKSTAND_TEST_CASE(form_bytes_print_as_base64_and_hex, unit) {
    char text[64];

    inkstand_form_bytes_base64(k_sixteen, sizeof k_sixteen, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "1PG7OiApB1nwvP+rz05pAQ==") != 0,
                          "base64 should be the standard, padded alphabet");
    inkstand_form_bytes_hex(k_sixteen, sizeof k_sixteen, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(strcmp(text, "d4f1bb3a20290759f0bcffabcf4e6901") != 0,
                          "hex should be two lower-case digits a byte");

    /* A short buffer stops at the last whole byte rather than printing half of one. */
    char short_text[6];
    inkstand_form_bytes_hex(k_sixteen, sizeof k_sixteen, short_text, sizeof short_text);
    INKSTAND_TEST_FAIL_IF(strcmp(short_text, "d4f1") != 0,
                          "hex should stop at the last byte that fits whole");
    record_success(test_name);
}

INKSTAND_TEST_CASE(form_bytes_parse_base64_or_hex_at_the_callers_sizes, unit) {
    uint8_t parsed[32];
    size_t len = 99U;

    INKSTAND_TEST_FAIL_IF(!inkstand_form_bytes_parse("1PG7OiApB1nwvP+rz05pAQ==", k_sizes,
                                                     K_SIZE_COUNT, parsed, sizeof parsed, &len) ||
                              len != 16U || memcmp(parsed, k_sixteen, 16U) != 0,
                          "base64 should parse back");
    INKSTAND_TEST_FAIL_IF(!inkstand_form_bytes_parse("d4f1bb3a20290759f0bcffabcf4e6901", k_sizes,
                                                     K_SIZE_COUNT, parsed, sizeof parsed, &len) ||
                              len != 16U || memcmp(parsed, k_sixteen, 16U) != 0,
                          "hex at one of the caller's sizes should parse as hex");
    INKSTAND_TEST_FAIL_IF(
        !inkstand_form_bytes_parse("AQ==", k_sizes, K_SIZE_COUNT, parsed, sizeof parsed, &len) ||
            len != 1U || parsed[0] != 1U,
        "a one-byte value should parse");
    INKSTAND_TEST_FAIL_IF(
        !inkstand_form_bytes_parse("", k_sizes, K_SIZE_COUNT, parsed, sizeof parsed, &len) ||
            len != 0U,
        "an empty row should be no bytes");

    /*
     * The sizes are what settle an ambiguous string. "abcd" is four hex digits and valid
     * base64 too: with 2 bytes among the sizes it is hex, and without, it is base64's 3 bytes.
     */
    static const size_t k_two[] = {2U};
    INKSTAND_TEST_FAIL_IF(
        !inkstand_form_bytes_parse("abcd", k_two, 1U, parsed, sizeof parsed, &len) || len != 2U ||
            parsed[0] != 0xAB || parsed[1] != 0xCD,
        "hex digits at a listed size should read as hex");
    INKSTAND_TEST_FAIL_IF(
        !inkstand_form_bytes_parse("abcd", k_sizes, K_SIZE_COUNT, parsed, sizeof parsed, &len) ||
            len != 3U,
        "the same digits at an unlisted size should read as base64");
    INKSTAND_TEST_FAIL_IF(
        !inkstand_form_bytes_parse("abcd", NULL, 0U, parsed, sizeof parsed, &len) || len != 3U,
        "no sizes at all should mean base64 only");
    record_success(test_name);
}

INKSTAND_TEST_CASE(form_bytes_refuse_what_is_not_exactly_bytes, unit) {
    uint8_t parsed[32];
    size_t len = 0U;

    /* A mistyped key decodes to a plausible wrong key, so padding must be exact. */
    INKSTAND_TEST_FAIL_IF(
        inkstand_form_bytes_parse("1PG7OiApB1nwvP+rz05pAQ=", k_sizes, K_SIZE_COUNT, parsed,
                                  sizeof parsed, &len) ||
            inkstand_form_bytes_parse("1PG7Oi=pB1nwvP+rz05pAQ==", k_sizes, K_SIZE_COUNT, parsed,
                                      sizeof parsed, &len) ||
            inkstand_form_bytes_parse("not a key!", k_sizes, K_SIZE_COUNT, parsed, sizeof parsed,
                                      &len) ||
            inkstand_form_bytes_parse("abc", k_sizes, K_SIZE_COUNT, parsed, sizeof parsed, &len),
        "short padding, padding inside, rubbish and an odd length should be refused");

    /* More bytes than the caller has room for, either spelling. */
    uint8_t small[8];
    INKSTAND_TEST_FAIL_IF(inkstand_form_bytes_parse("d4f1bb3a20290759f0bcffabcf4e6901", k_sizes,
                                                    K_SIZE_COUNT, small, sizeof small, &len) ||
                              inkstand_form_bytes_parse("1PG7OiApB1nwvP+rz05pAQ==", k_sizes,
                                                        K_SIZE_COUNT, small, sizeof small, &len),
                          "a value larger than the buffer should be refused");
    record_success(test_name);
}

INKSTAND_TEST_CASE(form_bytes_round_trip_through_either_spelling, unit) {
    uint8_t all[32];
    for (unsigned i = 0; i < 32U; ++i) {
        all[i] = (uint8_t)(i * 7U);
    }
    char text[80];
    uint8_t parsed[32];
    size_t len = 0U;

    inkstand_form_bytes_base64(all, sizeof all, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(
        strlen(text) != 44U ||
            !inkstand_form_bytes_parse(text, k_sizes, K_SIZE_COUNT, parsed, sizeof parsed, &len) ||
            len != 32U || memcmp(parsed, all, 32U) != 0,
        "32 bytes should round-trip through base64");
    inkstand_form_bytes_hex(all, sizeof all, text, sizeof text);
    INKSTAND_TEST_FAIL_IF(
        !inkstand_form_bytes_parse(text, k_sizes, K_SIZE_COUNT, parsed, sizeof parsed, &len) ||
            len != 32U || memcmp(parsed, all, 32U) != 0,
        "32 bytes should round-trip through hex");
    record_success(test_name);
}
