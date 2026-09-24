/*
 * The field reader: one record line's value, read into typed destinations.
 *
 * These came from mesh-client's ui_store suite, where they held the handshake cache's reader.
 * Every well-formed spelling here is one a writer there actually produced - "%u" for a flag, "%d"
 * for a signal strength, "%f" for a ratio - so this is a record format's own vocabulary rather
 * than a parser exercised on invented text.
 */
#include "framework/inkstand_test.h"

#include "inkstand/persist/fields.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

INKSTAND_TEST_CASE(persist_fields_read_every_type, unit) {
    uint32_t stamp = 0U;
    bool flag = false;
    uint8_t narrow = 0U;
    uint16_t medium = 0U;
    int32_t wide = 0;
    int16_t level = 0;
    float ratio = 0.0f;
    const struct inkstand_field fields[] = {
        INKSTAND_FIELD(&stamp),  INKSTAND_FIELD(&flag), INKSTAND_FIELD(&narrow),
        INKSTAND_FIELD(&medium), INKSTAND_FIELD(&wide), INKSTAND_FIELD(&level),
        INKSTAND_FIELD(&ratio),
    };
    const size_t count = sizeof fields / sizeof fields[0];

    INKSTAND_TEST_FAIL_IF(inkstand_fields_read("1749000000,1,101,65535,-680012345,-97,-3.500000",
                                               fields, count) != count,
                          "a well-formed value did not fill every field");
    INKSTAND_TEST_FAIL_IF(stamp != 1749000000U, "an unsigned field did not read");
    INKSTAND_TEST_FAIL_IF(!flag, "a flag field did not read");
    INKSTAND_TEST_FAIL_IF(narrow != 101U, "a byte field did not read");
    INKSTAND_TEST_FAIL_IF(medium != 65535U, "a 16-bit field did not read");
    INKSTAND_TEST_FAIL_IF(wide != -680012345, "a signed field did not read");
    INKSTAND_TEST_FAIL_IF(level != -97, "a 16-bit signed field did not read");
    INKSTAND_TEST_FAIL_IF(ratio > -3.49f || ratio < -3.51f, "a real field did not read");

    /* A flag is "not zero" rather than "exactly 1", so a hand-edited 2 is still true rather
       than a dropped record. */
    flag = false;
    INKSTAND_TEST_FAIL_IF(!inkstand_fields_read("2", &fields[1], 1U) || !flag,
                          "a flag other than 0 or 1 did not read as true");

    record_success(test_name);
}

/*
 * What the reader refuses, and why each one matters.
 *
 * A record file is text a user can edit, so it is an ingress like a network is - the same
 * reasoning that made inkstand_key_lookup() stricter than the sscanf it replaced. Every line
 * below is one glibc's "%u" and "%d" would have accepted, three of them by quietly producing a
 * plausible number out of an impossible one.
 */
INKSTAND_TEST_CASE(persist_fields_refuse_what_is_not_a_number, unit) {
    static const struct {
        const char *value;
        const char *why;
    } k_refused[] = {
        {"", "an empty value"},
        {"abc", "a value that is not a number at all"},
        {"12abc", "a number with rubbish after it"},
        {" 12", "a number behind whitespace the format never writes"},
        {"-1", "a negative number in an unsigned field, which \"%u\" wraps to 4294967295"},
        {"4294967296", "a number one past what the field can hold"},
        {"99999999999999999999999", "a number wider than strtoull itself"},
    };
    for (size_t i = 0U; i < sizeof k_refused / sizeof k_refused[0]; ++i) {
        uint32_t out = 0xABCDU;
        const struct inkstand_field field = INKSTAND_FIELD(&out);
        if (inkstand_fields_read(k_refused[i].value, &field, 1U) != 0U) {
            char detail[160];
            snprintf(detail, sizeof detail, "%s was read as a number", k_refused[i].why);
            INKSTAND_TEST_FAIL_IF(true, detail);
        }
        INKSTAND_TEST_FAIL_IF(out != 0xABCDU, "a refused field still wrote to its destination");
    }

    /* The narrow destinations, which a loader used to reach by scanning wide and casting down -
       so a percentage of 300 became 44 and a coordinate of 2^32 became 0. */
    uint8_t percent = 0U;
    const struct inkstand_field narrow = INKSTAND_FIELD(&percent);
    INKSTAND_TEST_FAIL_IF(inkstand_fields_read("300", &narrow, 1U) != 0U || percent != 0U,
                          "a byte field truncated a value that does not fit it");

    int32_t coordinate = 0;
    const struct inkstand_field signed_field = INKSTAND_FIELD(&coordinate);
    INKSTAND_TEST_FAIL_IF(inkstand_fields_read("4294967296", &signed_field, 1U) != 0U ||
                              coordinate != 0,
                          "a number past INT32_MAX was not refused");

    int16_t small = 0;
    const struct inkstand_field small_field = INKSTAND_FIELD(&small);
    INKSTAND_TEST_FAIL_IF(inkstand_fields_read("-32769", &small_field, 1U) != 0U || small != 0,
                          "a number below INT16_MIN was not refused");

    float real = 1.0f;
    const struct inkstand_field real_field = INKSTAND_FIELD(&real);
    INKSTAND_TEST_FAIL_IF(inkstand_fields_read("1.5x", &real_field, 1U) != 0U || real != 1.0f,
                          "a real number with rubbish after it was read");

    record_success(test_name);
}

/*
 * How many fields arrived, which is the whole of what a format's compatibility rules turn on: a
 * key that grew a field on the end is read by comparing this against the length it had before.
 */
INKSTAND_TEST_CASE(persist_fields_count_the_leading_fields, unit) {
    uint32_t first = 0U;
    uint32_t second = 0U;
    uint32_t third = 7U;
    const struct inkstand_field fields[] = {
        INKSTAND_FIELD(&first),
        INKSTAND_FIELD(&second),
        INKSTAND_FIELD(&third),
    };
    const size_t count = sizeof fields / sizeof fields[0];

    INKSTAND_TEST_FAIL_IF(inkstand_fields_read("1,2", fields, count) != 2U,
                          "a short value did not report the fields it had");
    INKSTAND_TEST_FAIL_IF(first != 1U || second != 2U,
                          "a short value did not fill what it carried");
    INKSTAND_TEST_FAIL_IF(third != 7U, "an absent trailing field did not keep its default");

    /* The walk stops at the first field that does not convert, so the return is always a count
       of *leading* fields - what sscanf's return meant. */
    INKSTAND_TEST_FAIL_IF(inkstand_fields_read("1,,3", fields, count) != 1U,
                          "a gap in the middle did not stop the walk");

    /* And a value longer than the field list is not an error: it is a line from a writer that
       knew something this reader does not. */
    INKSTAND_TEST_FAIL_IF(inkstand_fields_read("4,5,6,7,8", fields, count) != count,
                          "extra fields past the list were treated as a failure");
    INKSTAND_TEST_FAIL_IF(first != 4U || second != 5U || third != 6U,
                          "a longer value did not fill the fields the list has");

    INKSTAND_TEST_FAIL_IF(inkstand_fields_read(NULL, fields, count) != 0U, "a NULL value was read");
    INKSTAND_TEST_FAIL_IF(inkstand_fields_read("1", NULL, count) != 0U,
                          "a NULL field list was read into");

    /* A field with nowhere to go stops the walk rather than writing through NULL. */
    const struct inkstand_field nowhere = {INKSTAND_FIELD_TYPE_U32, NULL};
    INKSTAND_TEST_FAIL_IF(inkstand_fields_read("1", &nowhere, 1U) != 0U,
                          "a field with no destination was counted as read");

    record_success(test_name);
}
