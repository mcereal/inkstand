/*
 * The key table: a line's key looked up by a loader, and spelled out by a writer.
 *
 * The lookup cases came from mesh-client's ui_store suite, where they held the handshake cache's
 * keys; here they run over a table of this suite's own, built to have the same hazards - a name
 * that is both a count and the rows it counts, and names that are prefixes of each other. The
 * writer cases are new: mesh-client held its writers only through a whole cache's round trip.
 */
#include "framework/inkstand_test.h"

#include "inkstand/persist/keys.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum test_key {
    TEST_KEY_NONE = 0,
    TEST_KEY_VALID,
    TEST_KEY_ITEM,
    TEST_KEY_ITEM_LONG,
    TEST_KEY_LINK,
    TEST_KEY_LINKS,
    TEST_KEY_SAMPLE_COUNT,
    TEST_KEY_SAMPLE,
    TEST_KEY_COUNT
};

static const struct inkstand_key k_rows[TEST_KEY_COUNT] = {
    {NULL, 0U, INKSTAND_KEY_KIND_PLAIN},
    INKSTAND_KEY("valid", INKSTAND_KEY_KIND_PLAIN),
    INKSTAND_KEY("item", INKSTAND_KEY_KIND_ROW),
    INKSTAND_KEY("item_long", INKSTAND_KEY_KIND_ROW),
    INKSTAND_KEY("link", INKSTAND_KEY_KIND_SLOT),
    INKSTAND_KEY("links", INKSTAND_KEY_KIND_ROW),
    /* One name, two keys, told apart by the bracket. */
    INKSTAND_KEY("sample", INKSTAND_KEY_KIND_PLAIN),
    INKSTAND_KEY("sample", INKSTAND_KEY_KIND_ROW),
};

static const struct inkstand_key_table k_table = {k_rows, TEST_KEY_COUNT};

/* ---- lookup -------------------------------------------------------------------------------- */

INKSTAND_TEST_CASE(persist_keys_name_and_kind, unit) {
    INKSTAND_TEST_FAIL_IF(strcmp(inkstand_key_name(&k_table, TEST_KEY_ITEM_LONG), "item_long") != 0,
                          "a key did not spell itself from the table");
    INKSTAND_TEST_FAIL_IF(k_rows[TEST_KEY_ITEM_LONG].length != 9U,
                          "INKSTAND_KEY did not take the literal's length");
    INKSTAND_TEST_FAIL_IF(inkstand_key_kind(&k_table, TEST_KEY_LINK) != INKSTAND_KEY_KIND_SLOT,
                          "a key did not report its kind");

    /* The reserved row, and anything past the end, are no key at all. */
    INKSTAND_TEST_FAIL_IF(inkstand_key_name(&k_table, TEST_KEY_NONE) != NULL,
                          "the reserved row 0 had a name");
    INKSTAND_TEST_FAIL_IF(inkstand_key_name(&k_table, TEST_KEY_COUNT) != NULL,
                          "a key past the end of the table had a name");
    INKSTAND_TEST_FAIL_IF(inkstand_key_name(&k_table, -1) != NULL, "a negative key had a name");
    INKSTAND_TEST_FAIL_IF(inkstand_key_kind(&k_table, TEST_KEY_COUNT) != INKSTAND_KEY_KIND_PLAIN,
                          "a key past the end of the table had a kind");
    INKSTAND_TEST_FAIL_IF(inkstand_key_name(NULL, TEST_KEY_VALID) != NULL,
                          "a NULL table named a key");

    record_success(test_name);
}

/*
 * What the lookup accepts, and what it refuses.
 *
 * A record file is text a user can edit, so a key is an ingress like a network is. The lookup
 * this replaced was a prefix strncmp against a hand-counted length followed by an sscanf that
 * ignored whatever trailed the bracket, which let `item[0]junk` through as row 0 and left
 * `item[99999999999]` to sscanf's undefined behaviour on overflow. This holds the replacement to
 * being exact in both directions.
 */
INKSTAND_TEST_CASE(persist_keys_lookup, unit) {
    uint32_t index = 0U;
    uint32_t sub = 0U;

    INKSTAND_TEST_FAIL_IF(inkstand_key_lookup(&k_table, "valid", &index, &sub) != TEST_KEY_VALID,
                          "a plain key did not resolve");
    INKSTAND_TEST_FAIL_IF(inkstand_key_lookup(&k_table, "item_long[7]", &index, &sub) !=
                              TEST_KEY_ITEM_LONG,
                          "a row key did not resolve");
    INKSTAND_TEST_FAIL_IF(index != 7U, "a row key did not yield its index");
    INKSTAND_TEST_FAIL_IF(inkstand_key_lookup(&k_table, "link[3.2]", &index, &sub) != TEST_KEY_LINK,
                          "a slot key did not resolve");
    INKSTAND_TEST_FAIL_IF(index != 3U || sub != 2U, "a slot key did not yield both numbers");

    /* `sample` is a count and `sample[0]` is one of what it counts: one name, two keys. */
    INKSTAND_TEST_FAIL_IF(inkstand_key_lookup(&k_table, "sample", &index, &sub) !=
                              TEST_KEY_SAMPLE_COUNT,
                          "the bare key resolved to the row");
    INKSTAND_TEST_FAIL_IF(inkstand_key_lookup(&k_table, "sample[4]", &index, &sub) !=
                              TEST_KEY_SAMPLE,
                          "the indexed key resolved to the count");

    /* A row key is not a plain key wearing brackets, and vice versa. */
    INKSTAND_TEST_FAIL_IF(inkstand_key_lookup(&k_table, "item_long", &index, &sub) != 0,
                          "a row key resolved without its brackets");
    INKSTAND_TEST_FAIL_IF(inkstand_key_lookup(&k_table, "valid[0]", &index, &sub) != 0,
                          "a plain key resolved with brackets");
    /* The prefix confusion a hand-counted length is one miscount away from: `item` must not
       answer for `item_long`, nor `link` for `links`. */
    INKSTAND_TEST_FAIL_IF(inkstand_key_lookup(&k_table, "links[1]", &index, &sub) != TEST_KEY_LINKS,
                          "links resolved as link");
    INKSTAND_TEST_FAIL_IF(inkstand_key_lookup(&k_table, "item[1]", &index, &sub) != TEST_KEY_ITEM,
                          "item resolved as item_long");

    static const char *const malformed[] = {
        "",                             /* nothing at all                          */
        "[0]",                          /* brackets with no name                   */
        "item[",                        /* an opened bracket                       */
        "item[]",                       /* no number                               */
        "item[1",                       /* never closed                            */
        "item[1]x",                     /* trailing rubbish after the close        */
        "item[1.2]",                    /* a slot on a key that takes a row        */
        "link[1]",                      /* a row on a key that takes a slot        */
        "link[1.2.3]",                  /* one number too many                     */
        "item[-1]",                     /* a sign, which the format never writes   */
        "item[99999999999999999]",      /* wider than the index can hold           */
        "item[9999999999999999999999]", /* wider than strtoul itself: ERANGE      */
        "unknown",                      /* a key the table does not have           */
    };
    for (size_t i = 0U; i < sizeof malformed / sizeof malformed[0]; ++i) {
        index = 0xFFFFFFFFU;
        sub = 0xFFFFFFFFU;
        if (inkstand_key_lookup(&k_table, malformed[i], &index, &sub) != 0) {
            char detail[128];
            snprintf(detail, sizeof detail, "malformed key '%s' resolved to something",
                     malformed[i]);
            INKSTAND_TEST_FAIL_IF(true, detail);
        }
        /* And it leaves nothing behind for the caller to index an array with. */
        INKSTAND_TEST_FAIL_IF(index != 0U || sub != 0U,
                              "a refused key still wrote an index back to the caller");
    }

    INKSTAND_TEST_FAIL_IF(inkstand_key_lookup(&k_table, NULL, &index, &sub) != 0,
                          "a NULL key did not refuse");
    INKSTAND_TEST_FAIL_IF(inkstand_key_lookup(NULL, "valid", &index, &sub) != 0,
                          "a NULL table resolved a key");
    /* Both outputs are optional; a caller that wants neither must not crash. */
    INKSTAND_TEST_FAIL_IF(inkstand_key_lookup(&k_table, "item[2]", NULL, NULL) != TEST_KEY_ITEM,
                          "a row key needed its output pointers");

    record_success(test_name);
}

/* ---- writers ------------------------------------------------------------------------------- */

/* What a writer put in `file`, read back into `out`. tmpfile() rather than open_memstream()
   because the second is POSIX and the first is C. */
static bool read_back(FILE *file, char *out, size_t size) {
    rewind(file);
    const size_t length = fread(out, 1U, size - 1U, file);
    out[length] = '\0';
    return ferror(file) == 0;
}

/* The formatted writers take a va_list; this is the wrapper an application writes over them. */
static int write_plain(FILE *file, int key, const char *fmt, ...)
    __attribute__((format(INKWELL_PRINTF_ARCHETYPE, 3, 4)));
static int write_plain(FILE *file, int key, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int rc = inkstand_key_vwrite(file, &k_table, key, fmt, args);
    va_end(args);
    return rc;
}

static int write_row(FILE *file, int key, uint32_t index, const char *fmt, ...)
    __attribute__((format(INKWELL_PRINTF_ARCHETYPE, 4, 5)));
static int write_row(FILE *file, int key, uint32_t index, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int rc = inkstand_key_vwrite_row(file, &k_table, key, index, fmt, args);
    va_end(args);
    return rc;
}

static int write_slot(FILE *file, int key, uint32_t index, uint32_t slot, const char *fmt, ...)
    __attribute__((format(INKWELL_PRINTF_ARCHETYPE, 5, 6)));
static int write_slot(FILE *file, int key, uint32_t index, uint32_t slot, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int rc = inkstand_key_vwrite_slot(file, &k_table, key, index, slot, fmt, args);
    va_end(args);
    return rc;
}

/* Each writer spells the brackets its name says, and what it writes the lookup reads back. */
INKSTAND_TEST_CASE(persist_keys_writers_spell_the_brackets, unit) {
    FILE *file = tmpfile();
    INKSTAND_TEST_FAIL_IF(file == NULL, "no temporary file to write into");

    const int rcs[] = {
        write_plain(file, TEST_KEY_VALID, "%u", 1U),
        write_row(file, TEST_KEY_ITEM, 3U, "%d,%d", -4, 5),
        write_slot(file, TEST_KEY_LINK, 2U, 9U, "%u", 7U),
        inkstand_key_write_text(file, &k_table, TEST_KEY_SAMPLE_COUNT, "text"),
        inkstand_key_write_row_text(file, &k_table, TEST_KEY_SAMPLE, 0U, "row"),
        inkstand_key_write_slot_text(file, &k_table, TEST_KEY_LINK, 1U, 2U, "slot"),
    };
    for (size_t i = 0U; i < sizeof rcs / sizeof rcs[0]; ++i) {
        INKSTAND_TEST_FAIL_IF_CLEANUP(rcs[i] != 0, fclose(file), "a writer did not report success");
    }

    char written[256];
    const bool read = read_back(file, written, sizeof written);
    fclose(file);
    INKSTAND_TEST_FAIL_IF(!read, "the temporary file did not read back");
    INKSTAND_TEST_FAIL_IF(strcmp(written, "valid=1\n"
                                          "item[3]=-4,5\n"
                                          "link[2.9]=7\n"
                                          "sample=text\n"
                                          "sample[0]=row\n"
                                          "link[1.2]=slot\n") != 0,
                          "a writer did not spell its key and brackets");

    /* The round trip: the text left of each '=' resolves to the key that wrote it. */
    static const struct {
        const char *text;
        int key;
    } k_back[] = {{"valid", TEST_KEY_VALID},
                  {"item[3]", TEST_KEY_ITEM},
                  {"link[2.9]", TEST_KEY_LINK},
                  {"sample", TEST_KEY_SAMPLE_COUNT},
                  {"sample[0]", TEST_KEY_SAMPLE}};
    for (size_t i = 0U; i < sizeof k_back / sizeof k_back[0]; ++i) {
        INKSTAND_TEST_FAIL_IF(inkstand_key_lookup(&k_table, k_back[i].text, NULL, NULL) !=
                                  k_back[i].key,
                              "a written key did not read back as itself");
    }

    record_success(test_name);
}

/*
 * The escape: a `_text` writer's value cannot forge a second line or a second key.
 *
 * A value that came from a peer can carry anything, and a newline or an '=' inside it would
 * otherwise start a record of the peer's choosing. The formatted writers do not escape, which is
 * why they are for numbers.
 */
INKSTAND_TEST_CASE(persist_keys_text_writers_escape, unit) {
    FILE *file = tmpfile();
    INKSTAND_TEST_FAIL_IF(file == NULL, "no temporary file to write into");

    INKSTAND_TEST_FAIL_IF_CLEANUP(
        inkstand_key_write_text(file, &k_table, TEST_KEY_VALID, "a\nvalid=0\\b") != 0, fclose(file),
        "an escaped writer did not report success");

    char written[128];
    const bool read = read_back(file, written, sizeof written);
    fclose(file);
    INKSTAND_TEST_FAIL_IF(!read, "the temporary file did not read back");
    INKSTAND_TEST_FAIL_IF(strcmp(written, "valid=a\\x0avalid\\x3d0\\x5cb\n") != 0,
                          "a text value was not escaped");
    INKSTAND_TEST_FAIL_IF(strchr(written, '\n') != written + strlen(written) - 1U,
                          "an escaped value still carried a second line");

    record_success(test_name);
}

/* A key the table does not have writes nothing at all, rather than a line a loader would skip. */
INKSTAND_TEST_CASE(persist_keys_unknown_key_writes_nothing, unit) {
    FILE *file = tmpfile();
    INKSTAND_TEST_FAIL_IF(file == NULL, "no temporary file to write into");

    /* And says so: a key that is not there is -ENOENT, a stream that is not there -EINVAL. */
    const int rcs[] = {
        write_plain(file, TEST_KEY_NONE, "%u", 1U),
        write_row(file, TEST_KEY_COUNT, 0U, "%u", 1U),
        write_slot(file, -1, 0U, 0U, "%u", 1U),
        inkstand_key_write_text(file, &k_table, TEST_KEY_COUNT, "x"),
        inkstand_key_write_row_text(file, NULL, TEST_KEY_ITEM, 0U, "x"),
    };
    for (size_t i = 0U; i < sizeof rcs / sizeof rcs[0]; ++i) {
        INKSTAND_TEST_FAIL_IF_CLEANUP(rcs[i] != -ENOENT, fclose(file),
                                      "a key outside the table was not refused as -ENOENT");
    }
    INKSTAND_TEST_FAIL_IF_CLEANUP(
        inkstand_key_write_slot_text(NULL, &k_table, TEST_KEY_LINK, 0U, 0U, "x") != -EINVAL,
        fclose(file), "a NULL stream was not refused as -EINVAL");

    char written[64];
    const bool read = read_back(file, written, sizeof written);
    fclose(file);
    INKSTAND_TEST_FAIL_IF(!read, "the temporary file did not read back");
    INKSTAND_TEST_FAIL_IF(written[0] != '\0', "a key outside the table wrote a line");

    record_success(test_name);
}

/*
 * A write the stream could not take is reported, not swallowed.
 *
 * A stream opened for reading is the portable way to make every write fail - /dev/full is
 * Linux's - and the failure it gives is the one a full disk gives a caller: the line is not on
 * disk, and the writer says so. The indicator is sticky, so the next line reports it too.
 */
INKSTAND_TEST_CASE(persist_keys_writers_report_a_failed_stream, unit) {
    static const char k_path[] = "persist_keys_read_only.tmp";
    FILE *create = fopen(k_path, "w");
    INKSTAND_TEST_FAIL_IF(create == NULL, "could not create the file to reopen");
    fclose(create);

    FILE *file = fopen(k_path, "r");
    INKSTAND_TEST_FAIL_IF_CLEANUP(file == NULL, remove(k_path), "could not reopen the file");

    const int first = write_plain(file, TEST_KEY_VALID, "%u", 1U);
    const int second = inkstand_key_write_text(file, &k_table, TEST_KEY_VALID, "x");
    fclose(file);
    remove(k_path);
    INKSTAND_TEST_FAIL_IF(first != -EIO, "a write to a read-only stream was not -EIO");
    INKSTAND_TEST_FAIL_IF(second != -EIO, "a stream already in error did not keep reporting it");

    record_success(test_name);
}
