/*
 * The scene runner: a script played against an off-screen capture, on a clock the script names.
 *
 * mesh-client's copy had no cases of its own - its ui_capture suite tests inkcell's capture, not
 * the script - so these are new, written over the smallest host and renderer that can say what
 * the runner does with them. The renderer is an inkcell_fb_app that remembers what it drew and
 * reports itself moving for as many frames as a case asks, which is what settling is measured
 * against; the host is a counter a press or a verb bumps.
 */
#include "framework/inkstand_test.h"

#include "inkstand/app/scene.h"

#include "inkcell/ui/fb_capture.h"
#include "inkcell/ui/fb_draw.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_SCENE_MAX_FRAMES 64U

/* ---- a renderer that moves for as long as it is told --------------------------------------- */

struct test_app {
    unsigned moving;
    unsigned renders;
    uint32_t drawn;
};

static void test_app_render(struct inkcell_draw_state *state, const void *snapshot, void *ctx) {
    (void)state;
    struct test_app *app = ctx;
    app->renders++;
    app->drawn = *(const uint32_t *)snapshot;
    if (app->moving > 0U) {
        app->moving--;
    }
}

static bool test_app_pending(void *ctx) {
    return ((struct test_app *)ctx)->moving > 0U;
}

/* ---- a host ------------------------------------------------------------------------------- */

struct test_host {
    struct test_app app;
    uint32_t value;
    bool dirty;
    uint32_t snapshot;
    unsigned presses;
    enum inkcell_key last_key;
    uint64_t ticked_ms;
    unsigned themed;
    unsigned seeded;
    const char *screen;
    /* How many frames a press sets moving. */
    unsigned press_moves;
};

static bool test_host_drain(void *userdata, void *snapshot) {
    struct test_host *host = userdata;
    if (!host->dirty) {
        return false;
    }
    host->dirty = false;
    *(uint32_t *)snapshot = host->value;
    return true;
}

static void test_host_refresh(void *userdata) {
    ((struct test_host *)userdata)->dirty = true;
}

static void test_host_press(void *userdata, enum inkcell_key key, uint32_t page_rows) {
    (void)page_rows;
    struct test_host *host = userdata;
    host->presses++;
    host->last_key = key;
    host->value++;
    host->dirty = true;
    host->app.moving = host->press_moves;
}

static void test_host_tick(void *userdata, uint64_t now_ms) {
    ((struct test_host *)userdata)->ticked_ms = now_ms;
}

static const char *test_host_screen(void *userdata) {
    return ((struct test_host *)userdata)->screen;
}

static void test_host_themed(void *userdata) {
    ((struct test_host *)userdata)->themed++;
}

static int test_seed_plain(struct inkstand_scene *scene, void *userdata) {
    (void)scene;
    struct test_host *host = userdata;
    host->seeded++;
    host->value = 100U;
    host->dirty = true;
    return 0;
}

static int test_seed_other(struct inkstand_scene *scene, void *userdata) {
    (void)scene;
    struct test_host *host = userdata;
    host->seeded++;
    host->value = 200U;
    host->dirty = true;
    return 0;
}

/* `bump` changes the value; `spin N` sets the renderer moving for N frames; `quiet` changes it
   and asks for no frame; `early` is setup-only; `refuse` fails. */
static int test_verb_bump(struct inkstand_scene *scene, char *args, void *userdata) {
    (void)scene;
    (void)args;
    struct test_host *host = userdata;
    host->value++;
    host->dirty = true;
    return 0;
}

static int test_verb_spin(struct inkstand_scene *scene, char *args, void *userdata) {
    struct test_host *host = userdata;
    const char *count = inkstand_scene_word(&args);
    unsigned frames = 0U;
    if (count == NULL) {
        return inkstand_scene_fail(scene, "'spin' needs a frame count");
    }
    const int status = inkstand_scene_number(scene, count, "spin", &frames);
    if (status < 0) {
        return status;
    }
    host->app.moving = frames;
    host->dirty = true;
    return 0;
}

static int test_verb_quiet(struct inkstand_scene *scene, char *args, void *userdata) {
    (void)scene;
    (void)args;
    struct test_host *host = userdata;
    host->value += 10U;
    host->dirty = true;
    return 0;
}

static int test_verb_early(struct inkstand_scene *scene, char *args, void *userdata) {
    (void)scene;
    (void)args;
    (void)userdata;
    return 0;
}

static int test_verb_refuse(struct inkstand_scene *scene, char *args, void *userdata) {
    (void)args;
    (void)userdata;
    return inkstand_scene_fail(scene, "refused on purpose");
}

static const struct inkstand_scene_seed test_seeds[] = {
    {"plain", test_seed_plain},
    {"other", test_seed_other},
};

static const struct inkstand_scene_verb test_verbs[] = {
    {"bump", 0U, test_verb_bump},
    {"spin", 0U, test_verb_spin},
    {"quiet", INKSTAND_SCENE_NO_FRAME, test_verb_quiet},
    {"early", INKSTAND_SCENE_SETUP, test_verb_early},
    {"refuse", 0U, test_verb_refuse},
};

/* ---- a sink ------------------------------------------------------------------------------- */

struct test_sink {
    unsigned frames;
    unsigned delays;
    unsigned delay_ms[TEST_SCENE_MAX_FRAMES];
    uint32_t drawn[TEST_SCENE_MAX_FRAMES];
    bool out_of_order;
    struct test_host *host;
};

static int test_sink_frame(void *userdata, unsigned index, const struct inkcell_capture *capture) {
    (void)capture;
    struct test_sink *sink = userdata;
    if (index != sink->frames || index >= TEST_SCENE_MAX_FRAMES) {
        sink->out_of_order = true;
        return -ERANGE;
    }
    sink->drawn[index] = sink->host->app.drawn;
    sink->frames++;
    return 0;
}

static int test_sink_delay(void *userdata, unsigned index, unsigned delay_ms) {
    struct test_sink *sink = userdata;
    if (index != sink->delays || index >= TEST_SCENE_MAX_FRAMES) {
        sink->out_of_order = true;
        return -ERANGE;
    }
    sink->delay_ms[index] = delay_ms;
    sink->delays++;
    return 0;
}

/* ---- the rig ------------------------------------------------------------------------------ */

struct test_rig {
    struct test_host host;
    struct test_sink sink;
    struct inkcell_capture *capture;
    struct inkstand_scene scene;
};

static const struct inkstand_scene_config test_config = {
    .frame_ms = 33U,
    .delay_ms = 140U,
    .settle_frames = 40U,
    .start_ms = 1000U,
};

static struct inkstand_scene_host test_host_for(struct test_rig *rig) {
    struct inkstand_scene_host host = {
        .capture = rig->capture,
        .snapshot = &rig->host.snapshot,
        .drain = test_host_drain,
        .refresh = test_host_refresh,
        .press = test_host_press,
        .tick = test_host_tick,
        .screen = test_host_screen,
        .themed = test_host_themed,
        .seeds = test_seeds,
        .seed_count = sizeof test_seeds / sizeof test_seeds[0],
        .verbs = test_verbs,
        .verb_count = sizeof test_verbs / sizeof test_verbs[0],
        .userdata = &rig->host,
    };
    return host;
}

/* A 64x48 capture with the test renderer behind it and a scene over both. 0 or a negative errno;
   test_rig_close() releases whatever opened. */
static int test_rig_open(struct test_rig *rig, const struct inkstand_scene_config *config) {
    memset(rig, 0, sizeof *rig);
    rig->host.screen = "home";
    rig->sink.host = &rig->host;
    const int opened = inkcell_capture_open(&rig->capture, 64U, 48U, 0);
    if (opened != 0) {
        return opened;
    }
    const struct inkcell_fb_app app = {
        .ctx = &rig->host.app,
        .render = test_app_render,
        .pending = test_app_pending,
    };
    inkcell_fb_set_app(inkcell_capture_state(rig->capture), &app);
    const struct inkstand_scene_host host = test_host_for(rig);
    const struct inkstand_scene_sink sink = {
        .frame = test_sink_frame,
        .delay = test_sink_delay,
        .userdata = &rig->sink,
    };
    return inkstand_scene_init(&rig->scene, &host, &sink, config);
}

static void test_rig_close(struct test_rig *rig) {
    if (rig->capture != NULL) {
        inkcell_capture_close(rig->capture);
        rig->capture = NULL;
    }
}

/* Runs `script`, newline-separated, and finishes the scene when every line ran. Returns the first
   failure, or finish()'s answer. */
static int test_rig_run(struct test_rig *rig, const char *script) {
    char text[1024];
    snprintf(text, sizeof text, "%s", script);
    char *line = text;
    while (line != NULL) {
        char *next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        const int status = inkstand_scene_run_line(&rig->scene, line);
        if (status < 0) {
            return status;
        }
        line = next;
    }
    return inkstand_scene_finish(&rig->scene);
}

/* ---- cases -------------------------------------------------------------------------------- */

INKSTAND_TEST_CASE(scene_refuses_a_host_it_cannot_run, unit) {
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF_CLEANUP(test_rig_open(&rig, &test_config) != 0, test_rig_close(&rig),
                                  "rig open failed");

    static const struct inkstand_scene_verb shadowing[] = {{"key", 0U, test_verb_bump}};
    static const struct inkstand_scene_verb twice[] = {
        {"bump", 0U, test_verb_bump},
        {"bump", 0U, test_verb_quiet},
    };
    const char *failure = NULL;
    struct inkstand_scene scene;
    struct inkstand_scene_host host = test_host_for(&rig);
    host.drain = NULL;
    if (inkstand_scene_init(&scene, &host, NULL, NULL) != -EINVAL) {
        failure = "a host with no drain must be refused";
    }
    host = test_host_for(&rig);
    host.verbs = shadowing;
    host.verb_count = 1U;
    if (failure == NULL && inkstand_scene_init(&scene, &host, NULL, NULL) != -EINVAL) {
        failure = "a verb the runner answers first could never run, and must be refused";
    }
    host.verbs = twice;
    host.verb_count = 2U;
    if (failure == NULL && inkstand_scene_init(&scene, &host, NULL, NULL) != -EINVAL) {
        failure = "a verb named twice has a second row no script can reach, and must be refused";
    }
    if (failure == NULL && strstr(inkstand_scene_error(&scene), "bump") == NULL) {
        failure = "the refusal must name the verb";
    }
    test_rig_close(&rig);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

INKSTAND_TEST_CASE(scene_that_only_sets_up_still_owes_one_frame, unit) {
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF_CLEANUP(test_rig_open(&rig, &test_config) != 0, test_rig_close(&rig),
                                  "rig open failed");
    const int status = test_rig_run(&rig, "# a comment\n\ndelay 90\nscene other");

    const char *failure = NULL;
    if (status != 0) {
        failure = "a script of setup lines must run";
    } else if (rig.sink.frames != 1U || rig.sink.delays != 1U) {
        failure = "finish() must start the scene and emit its first frame";
    } else if (rig.sink.delay_ms[0] != 90U) {
        failure = "the frame must carry the delay the script set";
    } else if (rig.sink.drawn[0] != 200U || rig.host.seeded != 1U) {
        failure = "the seed the script named must be the one that ran, once";
    } else if (rig.host.themed != 1U) {
        failure = "themed() is told once the seed has run";
    }
    test_rig_close(&rig);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

INKSTAND_TEST_CASE(scene_hold_lengthens_a_frame_rather_than_adding_one, unit) {
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF_CLEANUP(test_rig_open(&rig, &test_config) != 0, test_rig_close(&rig),
                                  "rig open failed");
    const int status = test_rig_run(&rig, "frame\nhold 500\nhold 250");

    const char *failure = NULL;
    if (status != 0) {
        failure = "the script must run";
    } else if (rig.sink.frames != 2U) {
        failure = "a hold is the last frame lingering, not another frame";
    } else if (rig.sink.delay_ms[0] != 140U || rig.sink.delay_ms[1] != 140U + 500U + 250U) {
        failure = "both holds must lengthen the frame just emitted";
    } else if (rig.host.ticked_ms != 1000U + 750U) {
        failure = "a hold must move the host's clock with it";
    }
    test_rig_close(&rig);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

INKSTAND_TEST_CASE(scene_press_is_filmed_until_it_settles, unit) {
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF_CLEANUP(test_rig_open(&rig, &test_config) != 0, test_rig_close(&rig),
                                  "rig open failed");
    rig.host.press_moves = 3U;
    const int status = test_rig_run(&rig, "key a");

    /* The first frame, the press's, and two more while the renderer says it is still moving. */
    static const unsigned expected[] = {140U, 33U, 33U, 140U};
    const char *failure = NULL;
    if (status != 0) {
        failure = "the script must run";
    } else if (rig.host.presses != 1U || rig.host.last_key != inkcell_key_from_name("a")) {
        failure = "`key a` must press a, once";
    } else if (rig.sink.frames != 4U) {
        failure = "a press must be filmed until the renderer stops moving";
    } else if (memcmp(rig.sink.delay_ms, expected, sizeof expected) != 0) {
        failure =
            "a moving frame carries the frame interval, and the landing one the scene's delay";
    } else if (rig.sink.drawn[1] != 101U) {
        failure = "the press's frame must draw what the press changed";
    }
    test_rig_close(&rig);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

INKSTAND_TEST_CASE(scene_settle_stops_at_its_cap, unit) {
    struct inkstand_scene_config config = test_config;
    config.settle_frames = 5U;
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF_CLEANUP(test_rig_open(&rig, &config) != 0, test_rig_close(&rig),
                                  "rig open failed");
    const int status = test_rig_run(&rig, "spin 1000");

    const char *failure = NULL;
    if (status != 0) {
        failure = "the script must run";
    } else if (rig.sink.frames != 1U + 1U + 5U) {
        failure = "something that never settles is filmed for the cap and no longer";
    }
    test_rig_close(&rig);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

INKSTAND_TEST_CASE(scene_frames_an_application_verb_unless_told_not_to, unit) {
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF_CLEANUP(test_rig_open(&rig, &test_config) != 0, test_rig_close(&rig),
                                  "rig open failed");
    const int status = test_rig_run(&rig, "bump\nquiet\nspin 2");

    const char *failure = NULL;
    if (status != 0) {
        failure = "the script must run";
    } else if (rig.sink.frames != 1U + 1U + 2U) {
        failure = "a verb gets a frame and its settling; a NO_FRAME verb gets neither";
    } else if (rig.sink.drawn[1] != 101U) {
        failure = "the verb's frame must draw what the verb changed";
    } else if (rig.sink.drawn[2] != 111U) {
        failure = "a NO_FRAME verb's change still reaches the next frame";
    }
    test_rig_close(&rig);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

INKSTAND_TEST_CASE(scene_refuses_setup_after_the_first_frame, unit) {
    static const char *const scripts[] = {"frame\nscale 3", "frame\nearly",
                                          "frame\nclock "
                                          "2026-01-12 19:12"};
    const char *failure = NULL;
    for (size_t i = 0U; i < sizeof scripts / sizeof scripts[0] && failure == NULL; ++i) {
        struct test_rig rig;
        if (test_rig_open(&rig, &test_config) != 0) {
            failure = "rig open failed";
        } else if (test_rig_run(&rig, scripts[i]) != -EINVAL) {
            failure = "a setup line after the first frame must be refused";
        } else if (inkstand_scene_error_line(&rig.scene) != 2U) {
            failure = "the refusal must name the line it was on";
        } else if (strstr(inkstand_scene_error(&rig.scene), "before the first frame") == NULL) {
            failure = "the refusal must say why";
        }
        test_rig_close(&rig);
    }
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

INKSTAND_TEST_CASE(scene_reports_the_line_a_script_went_wrong_on, unit) {
    static const struct {
        const char *script;
        unsigned line;
        const char *says;
    } cases[] = {
        {"frame\n\nnonsense", 3U, "unknown command 'nonsense'"},
        {"key nosuchbutton", 1U, "no button called"},
        {"hold 12x", 1U, "not a number"},
        {"scene missing\nframe", 2U, "no seed called 'missing'"},
        {"bump\nrefuse", 2U, "refused on purpose"},
        {"hold 10", 0U, NULL},
    };
    const char *failure = NULL;
    for (size_t i = 0U; i < sizeof cases / sizeof cases[0] && failure == NULL; ++i) {
        struct test_rig rig;
        if (test_rig_open(&rig, &test_config) != 0) {
            failure = "rig open failed";
        } else {
            const int status = test_rig_run(&rig, cases[i].script);
            if (cases[i].says == NULL) {
                if (status != 0) {
                    failure = "a hold as the first line starts the scene and holds its first frame";
                }
            } else if (status == 0) {
                failure = "a bad line must fail the script";
            } else if (inkstand_scene_error_line(&rig.scene) != cases[i].line) {
                failure = "the failure must name the line it was on";
            } else if (strstr(inkstand_scene_error(&rig.scene), cases[i].says) == NULL) {
                failure = "the failure must say what was wrong";
            }
        }
        test_rig_close(&rig);
    }
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

INKSTAND_TEST_CASE(scene_expect_screen_is_a_test_not_a_picture, unit) {
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF_CLEANUP(test_rig_open(&rig, &test_config) != 0, test_rig_close(&rig),
                                  "rig open failed");
    const char *failure = NULL;
    if (test_rig_run(&rig, "expect screen home") != 0) {
        failure = "the screen that is up must pass";
    } else if (rig.sink.frames != 1U) {
        failure = "an expectation draws nothing beyond the first frame";
    }
    test_rig_close(&rig);

    if (failure == NULL) {
        INKSTAND_TEST_FAIL_IF_CLEANUP(test_rig_open(&rig, &test_config) != 0, test_rig_close(&rig),
                                      "rig open failed");
        if (test_rig_run(&rig, "frame\nexpect screen settings") != -EINVAL) {
            failure = "a screen that is not up must fail";
        } else if (strstr(inkstand_scene_error(&rig.scene), "'home' is up") == NULL) {
            failure = "the failure must say which screen was up";
        }
        test_rig_close(&rig);
    }
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}

INKSTAND_TEST_CASE(scene_hands_the_sink_every_delay_once_and_in_order, unit) {
    struct test_rig rig;
    INKSTAND_TEST_FAIL_IF_CLEANUP(test_rig_open(&rig, &test_config) != 0, test_rig_close(&rig),
                                  "rig open failed");
    rig.host.press_moves = 2U;
    int status = test_rig_run(&rig, "key down 2\nhold 40\nframe");
    if (status == 0) {
        status = inkstand_scene_finish(&rig.scene);
    }

    const char *failure = NULL;
    if (status != 0) {
        failure = "the script must run";
    } else if (rig.sink.out_of_order) {
        failure = "frames and delays must arrive numbered in order";
    } else if (rig.sink.delays != rig.sink.frames) {
        failure = "every frame's delay arrives exactly once, a second finish() included";
    }
    test_rig_close(&rig);
    INKSTAND_TEST_FAIL_IF(failure != NULL, failure);
    record_success(test_name);
}
