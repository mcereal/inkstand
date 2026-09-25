/*
 * The recently-used list: newest first, bounded, and one line in a file.
 *
 * mesh-client's preference suite still holds what its two lists *mean* - which peer is yours,
 * which transport an address belongs to, what an old file seeds - and it runs over this list
 * there. These are the cases for the half that came down: the order, the bound, the head that
 * reports nothing to write, the forget, an identity looser than the bytes, and the line.
 */
#include "framework/inkstand_test.h"

#include "inkstand/persist/recent.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- fixtures ------------------------------------------------------------------------------- */

#define NUMBERS_CAPACITY 4U

/* A list of plain numbers, compared byte for byte - the shape of a list of peer ids. */
struct numbers {
    uint32_t entries[NUMBERS_CAPACITY];
    struct inkstand_recent list;
};

static bool numbers_open(struct numbers *numbers) {
    memset(numbers, 0, sizeof *numbers);
    return inkstand_recent_init(&numbers->list, numbers->entries, sizeof numbers->entries[0],
                                NUMBERS_CAPACITY, 0U, NULL, NULL) == 0;
}

static bool note_number(struct numbers *numbers, uint32_t value) {
    return inkstand_recent_note(&numbers->list, &value);
}

static int rank_number(const struct numbers *numbers, uint32_t value) {
    return inkstand_recent_rank(&numbers->list, &value);
}

/* The list, newest first, is exactly `expected`. */
static bool numbers_are(const struct numbers *numbers, const uint32_t *expected, size_t count) {
    if (numbers->list.count != count) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (numbers->entries[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

static bool parse_number(const char *text, size_t len, void *entry, void *context) {
    (void)context;
    char digits[16];
    if (len == 0U || len >= sizeof digits || !isdigit((unsigned char)text[0])) {
        return false;
    }
    memcpy(digits, text, len);
    digits[len] = '\0';
    char *end = NULL;
    const unsigned long parsed = strtoul(digits, &end, 10);
    if (end != digits + len) {
        return false;
    }
    *(uint32_t *)entry = (uint32_t)parsed;
    return true;
}

static int write_number(FILE *out, const void *entry, void *context) {
    (void)context;
    return fprintf(out, "%lu", (unsigned long)*(const uint32_t *)entry) < 0 ? -EIO : 0;
}

/* A named thing whose name matches in either case - the shape of a list of addresses. */
struct named {
    char name[24];
};

static bool named_same(const void *entry, const void *wanted, void *context) {
    (void)context;
    const char *a = ((const struct named *)entry)->name;
    const char *b = ((const struct named *)wanted)->name;
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
    }
    return *a == *b;
}

static struct named named(const char *name) {
    struct named entry;
    memset(&entry, 0, sizeof entry);
    snprintf(entry.name, sizeof entry.name, "%s", name);
    return entry;
}

/* What write() puts on a stream, as a string. */
static bool written(const struct inkstand_recent *list, char separator,
                    inkstand_recent_write_fn write, char *out, size_t capacity) {
    FILE *stream = tmpfile();
    if (stream == NULL) {
        return false;
    }
    const int result = inkstand_recent_write(list, stream, separator, write, NULL);
    rewind(stream);
    const size_t got = fread(out, 1U, capacity - 1U, stream);
    out[got] = '\0';
    fclose(stream);
    return result == 0;
}

/* ---- the order ------------------------------------------------------------------------------ */

INKSTAND_TEST_CASE(recent_init_refuses_what_it_cannot_hold, unit) {
    struct inkstand_recent list;
    uint32_t entries[2];
    unsigned char big[INKSTAND_RECENT_ENTRY_MAX + 1U];

    INKSTAND_TEST_FAIL_IF(inkstand_recent_init(&list, NULL, 4U, 2U, 0U, NULL, NULL) != -EINVAL,
                          "a list over no array should be refused");
    INKSTAND_TEST_FAIL_IF(inkstand_recent_init(&list, entries, 0U, 2U, 0U, NULL, NULL) != -EINVAL,
                          "an entry of no size should be refused");
    INKSTAND_TEST_FAIL_IF(inkstand_recent_init(&list, entries, 4U, 0U, 0U, NULL, NULL) != -EINVAL,
                          "a list with room for nothing should be refused");
    INKSTAND_TEST_FAIL_IF(
        inkstand_recent_init(&list, big, sizeof big, 1U, 0U, NULL, NULL) != -EINVAL,
        "an entry past INKSTAND_RECENT_ENTRY_MAX should be refused - a note copies it first");

    /* A count past the array - a byte that went bad in a file - is clamped, not trusted. */
    INKSTAND_TEST_FAIL_IF(inkstand_recent_init(&list, entries, 4U, 2U, 200U, NULL, NULL) != 0 ||
                              list.count != 2U,
                          "a count past the capacity should be clamped to it");
    record_success(test_name);
}

/* Every refresh notes the current one again; only a change is worth a file write. */
INKSTAND_TEST_CASE(recent_note_puts_newest_first_and_reports_only_a_change, unit) {
    struct numbers numbers;
    INKSTAND_TEST_FAIL_IF(!numbers_open(&numbers), "init failed");

    INKSTAND_TEST_FAIL_IF(rank_number(&numbers, 7U) != -1, "an empty list should know nothing");
    INKSTAND_TEST_FAIL_IF(!note_number(&numbers, 7U) ||
                              !numbers_are(&numbers, (uint32_t[]){7U}, 1U),
                          "the first entry should be recorded");
    INKSTAND_TEST_FAIL_IF(note_number(&numbers, 7U) || numbers.list.count != 1U,
                          "re-noting the head should report nothing to write");
    INKSTAND_TEST_FAIL_IF(!note_number(&numbers, 9U) ||
                              !numbers_are(&numbers, (uint32_t[]){9U, 7U}, 2U),
                          "the newest should lead and the older survive");
    INKSTAND_TEST_FAIL_IF(rank_number(&numbers, 9U) != 0 || rank_number(&numbers, 7U) != 1,
                          "a rank is how recently, 0 the newest");

    /* Coming back to an older one moves it to the front rather than adding it twice. */
    INKSTAND_TEST_FAIL_IF(!note_number(&numbers, 7U) ||
                              !numbers_are(&numbers, (uint32_t[]){7U, 9U}, 2U),
                          "an entry already there should move to the front");
    record_success(test_name);
}

INKSTAND_TEST_CASE(recent_note_past_the_capacity_drops_the_oldest, unit) {
    struct numbers numbers;
    INKSTAND_TEST_FAIL_IF(!numbers_open(&numbers), "init failed");

    for (uint32_t i = 1U; i <= NUMBERS_CAPACITY; ++i) {
        (void)note_number(&numbers, i);
    }
    INKSTAND_TEST_FAIL_IF(!numbers_are(&numbers, (uint32_t[]){4U, 3U, 2U, 1U}, 4U),
                          "a full list should hold every entry, newest first");
    INKSTAND_TEST_FAIL_IF(!note_number(&numbers, 5U) ||
                              !numbers_are(&numbers, (uint32_t[]){5U, 4U, 3U, 2U}, 4U),
                          "one more should push the oldest off the end");

    /* Moving the last one up drops nothing: it was already counted. */
    INKSTAND_TEST_FAIL_IF(!note_number(&numbers, 2U) ||
                              !numbers_are(&numbers, (uint32_t[]){2U, 5U, 4U, 3U}, 4U),
                          "moving the oldest to the front should keep the rest");
    record_success(test_name);
}

/* The natural call hands note() a pointer into the list it is about to reorder. */
INKSTAND_TEST_CASE(recent_note_takes_an_entry_from_inside_the_list, unit) {
    struct numbers numbers;
    INKSTAND_TEST_FAIL_IF(!numbers_open(&numbers), "init failed");
    for (uint32_t i = 1U; i <= 3U; ++i) {
        (void)note_number(&numbers, i);
    }

    INKSTAND_TEST_FAIL_IF(!inkstand_recent_note(&numbers.list, &numbers.entries[2]) ||
                              !numbers_are(&numbers, (uint32_t[]){1U, 3U, 2U}, 3U),
                          "an entry noted by a pointer into the list should move whole");
    record_success(test_name);
}

INKSTAND_TEST_CASE(recent_forget_closes_the_gap_and_clears_the_slot, unit) {
    struct numbers numbers;
    INKSTAND_TEST_FAIL_IF(!numbers_open(&numbers), "init failed");
    for (uint32_t i = 1U; i <= 3U; ++i) {
        (void)note_number(&numbers, i);
    }

    const uint32_t absent = 42U;
    INKSTAND_TEST_FAIL_IF(inkstand_recent_forget(&numbers.list, &absent) ||
                              numbers.list.count != 3U,
                          "forgetting one not there should report it and change nothing");
    INKSTAND_TEST_FAIL_IF(!inkstand_recent_forget(&numbers.list, &numbers.entries[1]) ||
                              !numbers_are(&numbers, (uint32_t[]){3U, 1U}, 2U),
                          "forgetting the middle should close the gap in order");
    INKSTAND_TEST_FAIL_IF(numbers.entries[2] != 0U,
                          "the slot the list gave up should be zeroed, not left stale");
    INKSTAND_TEST_FAIL_IF(!inkstand_recent_forget(&numbers.list, &numbers.entries[0]) ||
                              !numbers_are(&numbers, (uint32_t[]){1U}, 1U),
                          "forgetting the head should promote the next one");
    record_success(test_name);
}

/* The caller says what the same entry is; the bytes are only the default. */
INKSTAND_TEST_CASE(recent_identity_is_the_callers, unit) {
    struct named entries[3];
    memset(entries, 0, sizeof entries);
    struct inkstand_recent list;
    INKSTAND_TEST_FAIL_IF(
        inkstand_recent_init(&list, entries, sizeof entries[0], 3U, 0U, named_same, NULL) != 0,
        "init failed");

    const struct named first = named("AA:01");
    const struct named second = named("BB:02");
    (void)inkstand_recent_note(&list, &first);
    (void)inkstand_recent_note(&list, &second);

    const struct named lower = named("aa:01");
    INKSTAND_TEST_FAIL_IF(inkstand_recent_rank(&list, &lower) != 1,
                          "the caller's identity should decide a rank");

    /* Moving takes the caller's copy, so the spelling noted last is the one kept. */
    INKSTAND_TEST_FAIL_IF(!inkstand_recent_note(&list, &lower) || list.count != 2U ||
                              strcmp(entries[0].name, "aa:01") != 0,
                          "a note of the same entry should move it and keep the new copy");
    record_success(test_name);
}

/* ---- the line ------------------------------------------------------------------------------- */

INKSTAND_TEST_CASE(recent_write_joins_newest_first_with_nothing_after, unit) {
    struct numbers numbers;
    INKSTAND_TEST_FAIL_IF(!numbers_open(&numbers), "init failed");

    char line[64];
    INKSTAND_TEST_FAIL_IF(!written(&numbers.list, ',', write_number, line, sizeof line) ||
                              line[0] != '\0',
                          "an empty list should write nothing at all");

    for (uint32_t i = 1U; i <= 3U; ++i) {
        (void)note_number(&numbers, i * 10U);
    }
    INKSTAND_TEST_FAIL_IF(!written(&numbers.list, ',', write_number, line, sizeof line) ||
                              strcmp(line, "30,20,10") != 0,
                          "entries should be separated, newest first, with no trailing separator");
    record_success(test_name);
}

/* The order is the value, so a line is read in file order and not replayed through note(). */
INKSTAND_TEST_CASE(recent_parse_reads_file_order_and_survives_a_bad_entry, unit) {
    struct numbers numbers;
    INKSTAND_TEST_FAIL_IF(!numbers_open(&numbers), "init failed");
    (void)note_number(&numbers, 99U);

    INKSTAND_TEST_FAIL_IF(
        inkstand_recent_parse(&numbers.list, "30,20,10", ',', parse_number, NULL) != 3U ||
            !numbers_are(&numbers, (uint32_t[]){30U, 20U, 10U}, 3U),
        "a line should replace the list, in file order");

    /* A refused entry is skipped and a second copy dropped; neither stops the rest. */
    INKSTAND_TEST_FAIL_IF(
        inkstand_recent_parse(&numbers.list, "5,x,,5,6", ',', parse_number, NULL) != 2U ||
            !numbers_are(&numbers, (uint32_t[]){5U, 6U}, 2U),
        "a bad entry or a repeat should be skipped and the line read on");
    INKSTAND_TEST_FAIL_IF(numbers.entries[2] != 0U || numbers.entries[3] != 0U,
                          "every slot past the count should be zeroed after a parse");

    INKSTAND_TEST_FAIL_IF(inkstand_recent_parse(&numbers.list, "1,2,3,4,5,6", ',', parse_number,
                                                NULL) != NUMBERS_CAPACITY ||
                              !numbers_are(&numbers, (uint32_t[]){1U, 2U, 3U, 4U}, 4U),
                          "a line longer than the list should stop at the capacity, newest kept");
    INKSTAND_TEST_FAIL_IF(inkstand_recent_parse(&numbers.list, "", ',', parse_number, NULL) != 0U,
                          "an empty line should be an empty list");
    record_success(test_name);
}

INKSTAND_TEST_CASE(recent_write_then_parse_round_trips, unit) {
    struct numbers numbers;
    INKSTAND_TEST_FAIL_IF(!numbers_open(&numbers), "init failed");
    for (uint32_t i = 1U; i <= 6U; ++i) {
        (void)note_number(&numbers, 0xA000U + i);
    }
    (void)note_number(&numbers, 0xA004U);

    char line[128];
    INKSTAND_TEST_FAIL_IF(!written(&numbers.list, ';', write_number, line, sizeof line),
                          "write failed");
    struct numbers loaded;
    INKSTAND_TEST_FAIL_IF(!numbers_open(&loaded), "init failed");
    INKSTAND_TEST_FAIL_IF(inkstand_recent_parse(&loaded.list, line, ';', parse_number, NULL) !=
                                  numbers.list.count ||
                              memcmp(loaded.entries, numbers.entries, sizeof numbers.entries) != 0,
                          "a list written and read back should come back whole and in order");
    record_success(test_name);
}
