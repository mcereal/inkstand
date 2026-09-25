#define _POSIX_C_SOURCE 200809L
/*
 * The journal: a directory of append-only files, one per subject.
 *
 * mesh-client's archive and trend-log suites still hold what its two journals *mean* - a
 * transcript that folds a re-delivered message, a chain whose first record after a restart is a
 * seam - and they run over this journal there. These are the cases for the half that came down:
 * the directory, the names, the append and what it reports, the filter's promise to leave a file
 * alone, the wipe's reach, and the ring. mesh-client held most of those only through a whole
 * store's round trip; here each is held on its own.
 */
#include "framework/inkstand_test.h"

#include "inkstand/persist/journal.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- fixtures ------------------------------------------------------------------------------- */

/* A fresh directory under /tmp, and the journal's own directory one level inside it - so init has
   a directory to make, which is the ordinary case. */
struct journal_fixture {
    char root[64];
    char dir[96];
};

static bool fixture_open(struct journal_fixture *fixture) {
    snprintf(fixture->root, sizeof fixture->root, "/tmp/inkstand_journal_XXXXXX");
    if (mkdtemp(fixture->root) == NULL) {
        return false;
    }
    snprintf(fixture->dir, sizeof fixture->dir, "%s/journal", fixture->root);
    return true;
}

static void remove_tree(const char *dir) {
    DIR *handle = opendir(dir);
    if (handle != NULL) {
        const struct dirent *entry = NULL;
        while ((entry = readdir(handle)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            char path[512];
            snprintf(path, sizeof path, "%s/%s", dir, entry->d_name);
            if (remove(path) != 0) {
                remove_tree(path);
            }
        }
        closedir(handle);
    }
    rmdir(dir);
}

static void fixture_close(struct journal_fixture *fixture) {
    remove_tree(fixture->root);
}

/* The whole of a file, for comparing before with after. */
static size_t slurp(const char *path, char *out, size_t capacity) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        out[0] = '\0';
        return 0U;
    }
    const size_t got = fread(out, 1U, capacity - 1U, file);
    out[got] = '\0';
    fclose(file);
    return got;
}

static bool spill(const char *path, const char *text) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }
    fputs(text, file);
    return fclose(file) == 0;
}

static bool file_exists(const char *path) {
    return access(path, F_OK) == 0;
}

/* An append that writes a fixed text and remembers whether it was told the file was resumed. */
struct append_probe {
    const char *text;
    bool called;
    bool resumed;
};

static void probe_write(FILE *file, bool resumed, void *context) {
    struct append_probe *probe = context;
    probe->called = true;
    probe->resumed = resumed;
    fputs(probe->text, file);
}

/* A reader that keeps the last value of each key it is shown and counts the lines. */
struct read_probe {
    unsigned lines;
    char last_key[32];
    char last_value[64];
};

static void probe_visit(void *context, const char *key, char *value) {
    struct read_probe *probe = context;
    probe->lines++;
    snprintf(probe->last_key, sizeof probe->last_key, "%s", key);
    snprintf(probe->last_value, sizeof probe->last_value, "%s", value);
}

static void write_fixed(FILE *file, void *context) {
    fputs(context, file);
}

/* ---- the directory -------------------------------------------------------------------------- */

INKSTAND_TEST_CASE(journal_init_makes_its_directory_and_accepts_one_already_there, unit) {
    struct journal_fixture fixture;
    INKSTAND_TEST_FAIL_IF(!fixture_open(&fixture), "could not make a temporary directory");

    struct inkstand_journal journal;
    const int first = inkstand_journal_init(&journal, fixture.dir, ".log", 0U);
    const bool made = file_exists(fixture.dir);
    const int second = inkstand_journal_init(&journal, fixture.dir, ".log", 0U);
    const bool enabled = inkstand_journal_enabled(&journal);

    fixture_close(&fixture);
    INKSTAND_TEST_FAIL_IF(first != 0 || !made, "the directory was not made");
    INKSTAND_TEST_FAIL_IF(second != 0 || !enabled,
                          "a directory already there was not taken as the journal's");
    record_success(test_name);
}

INKSTAND_TEST_CASE(journal_init_refuses_what_it_cannot_honour_and_stays_disabled, unit) {
    struct journal_fixture fixture;
    INKSTAND_TEST_FAIL_IF(!fixture_open(&fixture), "could not make a temporary directory");

    struct inkstand_journal journal;
    char nested[160];
    snprintf(nested, sizeof nested, "%s/missing/journal", fixture.root);
    char long_dir[INKSTAND_JOURNAL_DIR_MAX + 8U];
    memset(long_dir, 'd', sizeof long_dir - 1U);
    long_dir[0] = '/';
    long_dir[sizeof long_dir - 1U] = '\0';

    struct {
        const char *dir;
        const char *suffix;
        int expected;
    } const cases[] = {
        {NULL, ".log", -EINVAL},
        {"", ".log", -EINVAL},
        {fixture.dir, NULL, -EINVAL},
        {fixture.dir, "log", -EINVAL}, /* no dot */
        {fixture.dir, ".", -EINVAL},   /* nothing after it */
        {fixture.dir, "./x", -EINVAL}, /* not a plain word */
        {fixture.dir, ".a-suffix-far-too-long", -ENAMETOOLONG},
        {long_dir, ".log", -ENAMETOOLONG}, /* would have been truncated */
        {nested, ".log", -ENOENT},         /* one level only: the parent is missing */
    };
    for (size_t i = 0U; i < sizeof cases / sizeof cases[0]; ++i) {
        const int result = inkstand_journal_init(&journal, cases[i].dir, cases[i].suffix, 0U);
        if (result != cases[i].expected || inkstand_journal_enabled(&journal)) {
            fixture_close(&fixture);
            INKSTAND_TEST_FAIL_IF(true, "a refused init did not report its reason, or enabled");
        }
    }
    fixture_close(&fixture);
    record_success(test_name);
}

INKSTAND_TEST_CASE(journal_disabled_is_quiet, unit) {
    struct inkstand_journal journal;
    (void)inkstand_journal_init(&journal, "", ".log", 0U);

    struct append_probe probe = {"x=1\n", false, false};
    bool over = true;
    const int appended = inkstand_journal_append(&journal, "a", probe_write, &probe, &over);
    struct read_probe seen = {0};
    char line[64];
    const int read = inkstand_journal_read(&journal, "a", line, sizeof line, probe_visit, &seen);

    INKSTAND_TEST_FAIL_IF(appended != 0 || probe.called || over,
                          "an append to a disabled journal wrote, failed, or reported a cap");
    INKSTAND_TEST_FAIL_IF(read != -ENOENT || seen.lines != 0U,
                          "a disabled journal did not read as having no file");
    INKSTAND_TEST_FAIL_IF(inkstand_journal_replace(&journal, "a", write_fixed, "x=1\n") != 0 ||
                              inkstand_journal_forget(&journal, "a") != 0 ||
                              inkstand_journal_forget_all(&journal) != 0 ||
                              inkstand_journal_exists(&journal, "a"),
                          "a disabled journal was not quiet");
    record_success(test_name);
}

/* ---- names ---------------------------------------------------------------------------------- */

INKSTAND_TEST_CASE(journal_names_a_subject_only_with_a_plain_word, unit) {
    struct journal_fixture fixture;
    INKSTAND_TEST_FAIL_IF(!fixture_open(&fixture), "could not make a temporary directory");
    struct inkstand_journal journal;
    INKSTAND_TEST_FAIL_IF(inkstand_journal_init(&journal, fixture.dir, ".log", 0U) != 0,
                          "init failed");

    char path[INKSTAND_JOURNAL_PATH_MAX];
    char expected[INKSTAND_JOURNAL_PATH_MAX];
    snprintf(expected, sizeof expected, "%s/n1a2b-c_3.log", fixture.dir);
    const int named = inkstand_journal_path(&journal, "n1a2b-c_3", path, sizeof path);
    const bool right = named == 0 && strcmp(path, expected) == 0;

    char too_long[INKSTAND_JOURNAL_SUBJECT_MAX + 1U];
    memset(too_long, 'a', sizeof too_long - 1U);
    too_long[sizeof too_long - 1U] = '\0';
    const char *const refused[] = {NULL, "", ".", "..", "../up", "a/b", "a.b", "a b", too_long};
    bool all_refused = true;
    for (size_t i = 0U; i < sizeof refused / sizeof refused[0]; ++i) {
        all_refused = all_refused &&
                      inkstand_journal_path(&journal, refused[i], path, sizeof path) == -EINVAL;
    }
    char tiny[8];
    const int short_buffer = inkstand_journal_path(&journal, "a", tiny, sizeof tiny);

    fixture_close(&fixture);
    INKSTAND_TEST_FAIL_IF(!right, "a plain word did not name its file");
    INKSTAND_TEST_FAIL_IF(!all_refused, "a name that is not a plain word was accepted");
    INKSTAND_TEST_FAIL_IF(short_buffer != -ENAMETOOLONG, "a short buffer was not reported");
    record_success(test_name);
}

/* ---- appending and reading ------------------------------------------------------------------ */

INKSTAND_TEST_CASE(journal_append_says_whether_the_file_was_already_there, unit) {
    struct journal_fixture fixture;
    INKSTAND_TEST_FAIL_IF(!fixture_open(&fixture), "could not make a temporary directory");
    struct inkstand_journal journal;
    INKSTAND_TEST_FAIL_IF(inkstand_journal_init(&journal, fixture.dir, ".log", 0U) != 0,
                          "init failed");

    struct append_probe first = {"k=1\n", false, false};
    const int a = inkstand_journal_append(&journal, "s", probe_write, &first, NULL);
    struct append_probe second = {"k=2\n", false, false};
    const int b = inkstand_journal_append(&journal, "s", probe_write, &second, NULL);

    /* A file an open made and got no further with holds nothing to continue. */
    char empty[INKSTAND_JOURNAL_PATH_MAX];
    (void)inkstand_journal_path(&journal, "empty", empty, sizeof empty);
    const bool staged = spill(empty, "");
    struct append_probe third = {"k=3\n", false, false};
    const int c = inkstand_journal_append(&journal, "empty", probe_write, &third, NULL);

    struct read_probe seen = {0};
    char line[64];
    const int read = inkstand_journal_read(&journal, "s", line, sizeof line, probe_visit, &seen);

    fixture_close(&fixture);
    INKSTAND_TEST_FAIL_IF(a != 0 || b != 0 || c != 0 || !staged, "an append failed");
    INKSTAND_TEST_FAIL_IF(first.resumed, "a new file was reported as resumed");
    INKSTAND_TEST_FAIL_IF(!second.resumed, "a file an earlier append wrote was not resumed");
    INKSTAND_TEST_FAIL_IF(third.resumed,
                          "an empty file was reported as having something to continue");
    INKSTAND_TEST_FAIL_IF(read != 0 || seen.lines != 2U || strcmp(seen.last_value, "2") != 0,
                          "the two appends did not read back in order");
    record_success(test_name);
}

INKSTAND_TEST_CASE(journal_append_reports_the_cap_it_crosses, unit) {
    struct journal_fixture fixture;
    INKSTAND_TEST_FAIL_IF(!fixture_open(&fixture), "could not make a temporary directory");
    struct inkstand_journal capped;
    struct inkstand_journal uncapped;
    INKSTAND_TEST_FAIL_IF(inkstand_journal_init(&capped, fixture.dir, ".log", 8U) != 0 ||
                              inkstand_journal_init(&uncapped, fixture.dir, ".big", 0U) != 0,
                          "init failed");

    /* Four bytes, then eight - at the cap, which is not over it - then twelve. */
    struct append_probe probe = {"k=1\n", false, false};
    bool over[3] = {true, true, false};
    for (size_t i = 0U; i < 3U; ++i) {
        (void)inkstand_journal_append(&capped, "s", probe_write, &probe, &over[i]);
    }
    bool never = false;
    for (size_t i = 0U; i < 8U; ++i) {
        bool this_time = false;
        (void)inkstand_journal_append(&uncapped, "s", probe_write, &probe, &this_time);
        never = never || this_time;
    }

    fixture_close(&fixture);
    INKSTAND_TEST_FAIL_IF(over[0] || over[1], "a file at or under the cap was reported over it");
    INKSTAND_TEST_FAIL_IF(!over[2], "a file over the cap was not reported");
    INKSTAND_TEST_FAIL_IF(never, "an uncapped journal reported a cap");
    record_success(test_name);
}

INKSTAND_TEST_CASE(journal_read_skips_a_torn_final_append, unit) {
    struct journal_fixture fixture;
    INKSTAND_TEST_FAIL_IF(!fixture_open(&fixture), "could not make a temporary directory");
    struct inkstand_journal journal;
    INKSTAND_TEST_FAIL_IF(inkstand_journal_init(&journal, fixture.dir, ".log", 0U) != 0,
                          "init failed");
    char path[INKSTAND_JOURNAL_PATH_MAX];
    (void)inkstand_journal_path(&journal, "s", path, sizeof path);
    const bool staged = spill(path, "# a comment\nk=whole\nk=torn");

    struct read_probe seen = {0};
    char line[64];
    const int read = inkstand_journal_read(&journal, "s", line, sizeof line, probe_visit, &seen);
    const int missing =
        inkstand_journal_read(&journal, "nobody", line, sizeof line, probe_visit, &seen);

    fixture_close(&fixture);
    INKSTAND_TEST_FAIL_IF(!staged, "could not stage the file");
    INKSTAND_TEST_FAIL_IF(read != 0 || seen.lines != 1U || strcmp(seen.last_value, "whole") != 0,
                          "a torn final line was read, or a whole one was not");
    INKSTAND_TEST_FAIL_IF(missing != -ENOENT, "a subject with no file was not -ENOENT");
    record_success(test_name);
}

/* ---- replacing, filtering, forgetting ------------------------------------------------------- */

INKSTAND_TEST_CASE(journal_replace_leaves_no_temporary, unit) {
    struct journal_fixture fixture;
    INKSTAND_TEST_FAIL_IF(!fixture_open(&fixture), "could not make a temporary directory");
    struct inkstand_journal journal;
    INKSTAND_TEST_FAIL_IF(inkstand_journal_init(&journal, fixture.dir, ".log", 0U) != 0,
                          "init failed");

    struct append_probe probe = {"k=old\n", false, false};
    (void)inkstand_journal_append(&journal, "s", probe_write, &probe, NULL);
    const int replaced = inkstand_journal_replace(&journal, "s", write_fixed, "k=new\n");

    char path[INKSTAND_JOURNAL_PATH_MAX];
    char temp[INKSTAND_JOURNAL_PATH_MAX + 8U];
    (void)inkstand_journal_path(&journal, "s", path, sizeof path);
    snprintf(temp, sizeof temp, "%s.tmp", path);
    char body[64];
    slurp(path, body, sizeof body);
    const bool temp_left = file_exists(temp);

    fixture_close(&fixture);
    INKSTAND_TEST_FAIL_IF(replaced != 0 || strcmp(body, "k=new\n") != 0,
                          "the file was not replaced");
    INKSTAND_TEST_FAIL_IF(temp_left, "the temporary was left behind");
    record_success(test_name);
}

/* A filter that drops the one line equal to its target and holds each line back until the next,
   so `end` has something to settle - the shape a real one has, when whether a line is kept is
   only known from the line after it. */
struct drop_filter {
    const char *target;
    char held[64];
    bool holding;
    uint32_t dropped;
};

static void drop_line(void *context, const char *line, FILE *out) {
    struct drop_filter *filter = context;
    if (filter->holding) {
        fprintf(out, "%s\n", filter->held);
    }
    filter->holding = false;
    if (strcmp(line, filter->target) == 0) {
        filter->dropped++;
        return;
    }
    snprintf(filter->held, sizeof filter->held, "%s", line);
    filter->holding = true;
}

static uint32_t drop_end(void *context, FILE *out) {
    struct drop_filter *filter = context;
    if (filter->holding) {
        fprintf(out, "%s\n", filter->held);
    }
    return filter->dropped;
}

INKSTAND_TEST_CASE(journal_filter_drops_what_it_is_asked_and_keeps_the_rest_verbatim, unit) {
    struct journal_fixture fixture;
    INKSTAND_TEST_FAIL_IF(!fixture_open(&fixture), "could not make a temporary directory");
    struct inkstand_journal journal;
    INKSTAND_TEST_FAIL_IF(inkstand_journal_init(&journal, fixture.dir, ".log", 0U) != 0,
                          "init failed");
    char path[INKSTAND_JOURNAL_PATH_MAX];
    (void)inkstand_journal_path(&journal, "s", path, sizeof path);
    /* An escaped value stays escaped: the filter is shown the line as it sits on disk. */
    const bool staged = spill(path, "# kept\nk=a\\x3db\nk=drop\nk=last\n");

    struct drop_filter filter = {.target = "k=drop"};
    char line[64];
    const int dropped =
        inkstand_journal_filter(&journal, "s", line, sizeof line, drop_line, drop_end, &filter);
    char body[128];
    slurp(path, body, sizeof body);
    struct drop_filter none = {.target = "k=absent"};
    const int missing =
        inkstand_journal_filter(&journal, "nobody", line, sizeof line, drop_line, drop_end, &none);

    fixture_close(&fixture);
    INKSTAND_TEST_FAIL_IF(!staged, "could not stage the file");
    INKSTAND_TEST_FAIL_IF(dropped != 1, "the filter's count was not returned");
    INKSTAND_TEST_FAIL_IF(strcmp(body, "# kept\nk=a\\x3db\nk=last\n") != 0,
                          "the rest of the file did not come through verbatim, in order");
    INKSTAND_TEST_FAIL_IF(missing != 0, "a subject with no file had something dropped from it");
    record_success(test_name);
}

INKSTAND_TEST_CASE(journal_filter_that_drops_nothing_leaves_the_file_alone, unit) {
    struct journal_fixture fixture;
    INKSTAND_TEST_FAIL_IF(!fixture_open(&fixture), "could not make a temporary directory");
    struct inkstand_journal journal;
    INKSTAND_TEST_FAIL_IF(inkstand_journal_init(&journal, fixture.dir, ".log", 0U) != 0,
                          "init failed");
    char path[INKSTAND_JOURNAL_PATH_MAX];
    (void)inkstand_journal_path(&journal, "s", path, sizeof path);
    /* A line longer than the filter's buffer and a torn tail: a rewrite would lose both, so
       finding them still there is what proves there was no rewrite. */
    char original[256];
    snprintf(original, sizeof original, "k=1\nk=%080d\nk=torn", 0);
    const bool staged = spill(path, original);

    struct drop_filter filter = {.target = "k=absent"};
    char line[32];
    const int dropped =
        inkstand_journal_filter(&journal, "s", line, sizeof line, drop_line, drop_end, &filter);
    char body[256];
    slurp(path, body, sizeof body);
    char temp[INKSTAND_JOURNAL_PATH_MAX + 8U];
    snprintf(temp, sizeof temp, "%s.tmp", path);
    const bool temp_left = file_exists(temp);

    fixture_close(&fixture);
    INKSTAND_TEST_FAIL_IF(!staged, "could not stage the file");
    INKSTAND_TEST_FAIL_IF(dropped != 0, "a filter that dropped nothing reported a count");
    INKSTAND_TEST_FAIL_IF(strcmp(body, original) != 0, "the file was rewritten anyway");
    INKSTAND_TEST_FAIL_IF(temp_left, "the temporary was left behind");
    record_success(test_name);
}

INKSTAND_TEST_CASE(journal_forget_all_takes_only_its_own_files, unit) {
    struct journal_fixture fixture;
    INKSTAND_TEST_FAIL_IF(!fixture_open(&fixture), "could not make a temporary directory");
    struct inkstand_journal journal;
    INKSTAND_TEST_FAIL_IF(inkstand_journal_init(&journal, fixture.dir, ".trend", 0U) != 0,
                          "init failed");

    const char *const ours[] = {"a.trend", "b.trend", "c.trend.tmp"};
    const char *const theirs[] = {"notes.txt", "a.trend.bak", "a.trendy", ".trend"};
    char path[256];
    bool staged = true;
    for (size_t i = 0U; i < 3U; ++i) {
        snprintf(path, sizeof path, "%s/%s", fixture.dir, ours[i]);
        staged = staged && spill(path, "k=1\n");
    }
    for (size_t i = 0U; i < 4U; ++i) {
        snprintf(path, sizeof path, "%s/%s", fixture.dir, theirs[i]);
        staged = staged && spill(path, "k=1\n");
    }

    const int one = inkstand_journal_forget(&journal, "a");
    const int again = inkstand_journal_forget(&journal, "a");
    const int dropped = inkstand_journal_forget_all(&journal);

    bool ours_gone = true;
    for (size_t i = 0U; i < 3U; ++i) {
        snprintf(path, sizeof path, "%s/%s", fixture.dir, ours[i]);
        ours_gone = ours_gone && !file_exists(path);
    }
    bool theirs_kept = true;
    for (size_t i = 0U; i < 4U; ++i) {
        snprintf(path, sizeof path, "%s/%s", fixture.dir, theirs[i]);
        theirs_kept = theirs_kept && file_exists(path);
    }

    fixture_close(&fixture);
    INKSTAND_TEST_FAIL_IF(!staged, "could not stage the files");
    INKSTAND_TEST_FAIL_IF(one != 0 || again != 0, "forgetting one subject, twice, failed");
    INKSTAND_TEST_FAIL_IF(dropped != 1, "the wipe did not count only the subjects' files");
    INKSTAND_TEST_FAIL_IF(!ours_gone, "a file of the journal's, or its temporary, survived");
    INKSTAND_TEST_FAIL_IF(!theirs_kept, "a file that is not the journal's was removed");
    record_success(test_name);
}

/* ---- the ring ------------------------------------------------------------------------------- */

static void ring_fill(struct inkstand_journal_ring *ring, uint32_t from, uint32_t to) {
    for (uint32_t value = from; value < to; ++value) {
        uint32_t *slot = inkstand_journal_ring_push(ring);
        if (slot != NULL) {
            *slot = value;
        }
    }
}

INKSTAND_TEST_CASE(journal_ring_counts_a_drop_only_once_one_happens, unit) {
    uint32_t entries[4];
    struct inkstand_journal_ring ring;
    inkstand_journal_ring_init(&ring, entries, sizeof entries[0], 4U);

    ring_fill(&ring, 0U, 4U);
    const uint32_t full_dropped = ring.dropped;
    const uint32_t full_held = inkstand_journal_ring_held(&ring);
    ring_fill(&ring, 4U, 5U);
    const uint32_t one_more_dropped = ring.dropped;

    INKSTAND_TEST_FAIL_IF(full_dropped != 0U || full_held != 4U,
                          "a ring that was merely filled claimed to have dropped a record");
    INKSTAND_TEST_FAIL_IF(one_more_dropped != 1U, "the record pushed out was not counted");
    record_success(test_name);
}

INKSTAND_TEST_CASE(journal_ring_finishes_oldest_first, unit) {
    /* Every rotation a four-slot ring can be left in, and one that never filled. */
    for (uint32_t pushed = 0U; pushed <= 11U; ++pushed) {
        uint32_t entries[4] = {0};
        struct inkstand_journal_ring ring;
        inkstand_journal_ring_init(&ring, entries, sizeof entries[0], 4U);
        ring_fill(&ring, 0U, pushed);

        const uint32_t count = inkstand_journal_ring_finish(&ring);
        const uint32_t expected = pushed < 4U ? pushed : 4U;
        INKSTAND_TEST_FAIL_IF(count != expected, "finish returned the wrong count");
        const uint32_t oldest = pushed - count;
        for (uint32_t i = 0U; i < count; ++i) {
            INKSTAND_TEST_FAIL_IF(entries[i] != oldest + i, "the buffer is not oldest-first");
        }
        /* A second finish is harmless. */
        INKSTAND_TEST_FAIL_IF(inkstand_journal_ring_finish(&ring) != count ||
                                  (count > 0U && entries[0] != oldest),
                              "a second finish moved the records");
    }
    record_success(test_name);
}

INKSTAND_TEST_CASE(journal_ring_lets_a_reader_fold_a_record_it_holds, unit) {
    /* The archive's case: a record seen before is updated where it sits, not pushed again. */
    struct entry {
        uint32_t id;
        uint32_t state;
    } entries[3];
    struct inkstand_journal_ring ring;
    inkstand_journal_ring_init(&ring, entries, sizeof entries[0], 3U);

    const struct entry incoming[] = {{1U, 0U}, {2U, 0U}, {1U, 7U}, {3U, 0U}};
    for (size_t n = 0U; n < sizeof incoming / sizeof incoming[0]; ++n) {
        struct entry *held = NULL;
        for (uint32_t i = 0U; i < inkstand_journal_ring_held(&ring) && held == NULL; ++i) {
            struct entry *candidate = inkstand_journal_ring_at(&ring, i);
            if (candidate->id == incoming[n].id) {
                held = candidate;
            }
        }
        if (held == NULL) {
            held = inkstand_journal_ring_push(&ring);
        }
        *held = incoming[n];
    }
    const uint32_t count = inkstand_journal_ring_finish(&ring);

    INKSTAND_TEST_FAIL_IF(count != 3U || ring.dropped != 0U, "a folded record took a slot");
    INKSTAND_TEST_FAIL_IF(entries[0].id != 1U || entries[0].state != 7U || entries[1].id != 2U ||
                              entries[2].id != 3U,
                          "the fold did not keep the first position and the later contents");
    INKSTAND_TEST_FAIL_IF(inkstand_journal_ring_at(&ring, 3U) != NULL,
                          "a slot past what is held was handed out");
    record_success(test_name);
}

INKSTAND_TEST_CASE(journal_ring_without_room_hands_out_nothing, unit) {
    struct inkstand_journal_ring ring;
    inkstand_journal_ring_init(&ring, NULL, sizeof(uint32_t), 4U);
    INKSTAND_TEST_FAIL_IF(inkstand_journal_ring_push(&ring) != NULL ||
                              inkstand_journal_ring_finish(&ring) != 0U,
                          "a ring with no buffer handed out a slot");
    record_success(test_name);
}
