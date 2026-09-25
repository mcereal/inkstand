#pragma once

/*
 * The scene runner: a script of presses, played against an off-screen panel on a clock the script
 * names, with every frame it produces handed to a sink.
 *
 * It is the control socket's offline twin (inkstand/app/control.h). Both drive a program through
 * the path a button takes and name the screen by an ASCII id; the socket does it to a running
 * program on the real clock, and this does it to an application's store in memory, on a clock
 * that only moves when the script says so - which is what makes a transition reproducible frame
 * for frame, on any host, at any hour.
 *
 * This half names nothing of the application's. The application hands it a host - its store's
 * drain, a press, the housekeeping a loop turn does - and a table of verbs of its own, the
 * fixtures that put invented state behind the screens. The generic verbs are here:
 *
 *   scene NAME        which of the application's seeds to start from   (setup, default the first)
 *   scale N           glyph multiplier                                 (setup, default the theme's)
 *   delay MS          per-frame delay a frame somebody is meant to read carries (setup)
 *   clock YYYY-MM-DD HH:MM   pin the wall clock, as local time          (setup)
 *   theme NAME        before the first frame picks the look; after it switches and emits a frame
 *   pointer           draw the frame a window with a mouse gets
 *   key NAME [COUNT]  press, and film whatever the press set moving
 *   frame             emit the current screen again
 *   hold MS           lengthen the frame just emitted, and move the clock with it
 *   expect screen ID  fail unless the host's screen() says ID is up; draws nothing
 *
 * Every other verb is the application's, and by default emits one frame and plays out whatever
 * that frame left moving. The runner keeps that rule rather than each verb: when every verb had
 * to remember it, a quarter of them forgot, and a script's `hold` froze an animation on its
 * first frame.
 *
 * The verb table is the half of this header not yet proven general: one application has written
 * rows for it. It is marked unstable until a second one does - see docs/extraction.md.
 *
 * Nothing here exits. A verb that refuses returns inkstand_scene_fail(), the runner keeps the line
 * it was on, and the caller decides what a bad script costs - which is what lets a scene run
 * in-process under a test as well as behind a command-line tool.
 */

#include "inkcell/ui/key.h"
#include "inkstand/nav/frame_scheduler.h"
#include "inkwell/base/log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct inkcell_capture;
struct inkstand_scene;

/* Must come before the first frame; refused after it. The runner does not start the scene for
   one. */
#define INKSTAND_SCENE_SETUP 0x1U
/* Emits its own frames, or none: the runner neither emits nor settles after it. */
#define INKSTAND_SCENE_NO_FRAME 0x2U

/*
 * One of the application's verbs. `args` is the rest of the line, writable, for
 * inkstand_scene_word() to split. Returns 0, or inkstand_scene_fail().
 */
struct inkstand_scene_verb {
    const char *name;
    unsigned flags;
    int (*run)(struct inkstand_scene *scene, char *args, void *userdata);
};

/* A named starting state - `scene NAME`. The first row is the default. */
struct inkstand_scene_seed {
    const char *name;
    int (*seed)(struct inkstand_scene *scene, void *userdata);
};

/*
 * What the runner drives, named by what it asks.
 *
 * `drain` and `refresh` are the frame scheduler's: the runner is a presenter too, with a capture
 * where the panel would be. `press` is the path a button takes, handed the rows the last frame's
 * paged list had - the controller's job on the device. `tick` is the housekeeping a loop turn
 * does, on the scene's clock: a notice timing out is the usual case, and without it a notice
 * sliding away is not filmable at all. `screen` names what is up by the same ASCII id the control
 * socket's `screen` answers with, for `expect screen`. `themed` is told when the look is set or
 * changes, for an application that says what it is drawing with.
 *
 * `capture`, `snapshot`, `drain` and `press` are required; the rest may be NULL.
 */
struct inkstand_scene_host {
    struct inkcell_capture *capture;
    void *snapshot;
    inkstand_frame_drain_fn drain;
    inkstand_frame_refresh_fn refresh;
    void (*press)(void *userdata, enum inkcell_key key, uint32_t page_rows);
    void (*tick)(void *userdata, uint64_t now_ms);
    const char *(*screen)(void *userdata);
    void (*themed)(void *userdata);
    const struct inkstand_scene_seed *seeds;
    size_t seed_count;
    const struct inkstand_scene_verb *verbs;
    size_t verb_count;
    void *userdata;
};

/*
 * Where the frames go. `frame` is called as each is drawn - the capture holds it until the next -
 * and `delay` once that frame's delay is final, which is when the next frame is drawn or the
 * script ends: `hold` lengthens the frame already emitted, so its delay is not known when it is.
 * Frames are numbered from 0, and each `delay` arrives in order. Either may be NULL.
 */
struct inkstand_scene_sink {
    int (*frame)(void *userdata, unsigned index, const struct inkcell_capture *capture);
    int (*delay)(void *userdata, unsigned index, unsigned delay_ms);
    void *userdata;
};

/*
 * The numbers are the application's: how fine its panel shows motion, how long a frame somebody
 * is meant to read stays up, how many frames a widget that never settles is filmed for. Zero
 * takes the default beside each - except `delay_ms`, where zero is a delay somebody may mean,
 * and is kept.
 */
struct inkstand_scene_config {
    unsigned frame_ms;      /* 33: the interval a still-moving frame carries */
    unsigned delay_ms;      /* no default: 0 is a zero delay */
    unsigned settle_frames; /* 40 */
    uint64_t start_ms;      /* 1000: the scene clock at the first frame */
    int scale;              /* 0: the theme's own */
    const char *theme;      /* NULL: whatever the capture opened with */
};

#define INKSTAND_SCENE_NAME_MAX 32U
#define INKSTAND_SCENE_ERROR_MAX 256U

struct inkstand_scene {
    struct inkstand_scene_host host;
    struct inkstand_scene_sink sink;
    struct inkstand_scene_config config;
    char seed[INKSTAND_SCENE_NAME_MAX];
    char theme[INKSTAND_SCENE_NAME_MAX];
    bool started;
    bool finished;
    uint64_t now_ms;
    unsigned frames;
    /* The delay of the last frame emitted, which a `hold` may still lengthen. */
    unsigned pending_delay_ms;
    unsigned line;
    char error[INKSTAND_SCENE_ERROR_MAX];
};

/* Returns 0, or -EINVAL for a host missing something required, or a verb table naming one verb
   twice or naming one of the runner's own - a row whose name is already taken is one no script
   can ever reach. */
int inkstand_scene_init(struct inkstand_scene *scene, const struct inkstand_scene_host *host,
                        const struct inkstand_scene_sink *sink,
                        const struct inkstand_scene_config *config);

/* Runs one line. '#' starts a comment. Returns 0 or a negative errno, with the reason in
   inkstand_scene_error(). */
int inkstand_scene_run_line(struct inkstand_scene *scene, char *line);
/* Every line of `file`, stopping at the first that fails. */
int inkstand_scene_run_file(struct inkstand_scene *scene, FILE *file);
/* Starts the scene if nothing has - a script that only set up still owes one frame - and hands
   the last frame's delay to the sink. */
int inkstand_scene_finish(struct inkstand_scene *scene);

/* Why the last call failed, and on which line (1-based; 0 before any). */
const char *inkstand_scene_error(const struct inkstand_scene *scene);
unsigned inkstand_scene_error_line(const struct inkstand_scene *scene);

/* ---- for a verb ---------------------------------------------------------------------------- */

/* Records the reason and returns -EINVAL, so a verb can `return inkstand_scene_fail(...)`. */
int inkstand_scene_fail(struct inkstand_scene *scene, const char *format, ...)
    __attribute__((format(INKWELL_PRINTF_ARCHETYPE, 2, 3)));

/* Splits off the next whitespace-delimited word, leaving *rest on what follows. NULL at the end. */
char *inkstand_scene_word(char **rest);
/* The rest of the line, less leading whitespace. */
char *inkstand_scene_tail(char *rest);
/* A number, bounded as a typo guard rather than a claim about what renders. */
int inkstand_scene_number(struct inkstand_scene *scene, const char *text, const char *what,
                          unsigned *out);
int inkstand_scene_signed(struct inkstand_scene *scene, const char *text, const char *what,
                          int *out);

/* Draws the current state as the next frame. */
int inkstand_scene_emit(struct inkstand_scene *scene);
/* Steps the clock and draws until the last frame has stopped moving, or the settle cap. */
int inkstand_scene_settle(struct inkstand_scene *scene);
/* A press, the frame it produces, and whatever it set moving. */
int inkstand_scene_press(struct inkstand_scene *scene, enum inkcell_key key);

uint64_t inkstand_scene_now(const struct inkstand_scene *scene);
struct inkcell_capture *inkstand_scene_capture(const struct inkstand_scene *scene);
bool inkstand_scene_started(const struct inkstand_scene *scene);

#ifdef __cplusplus
}
#endif
