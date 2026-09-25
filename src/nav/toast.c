/*
 * A snackbar's notices - see inkstand/nav/toast.h.
 */

#include "inkstand/nav/toast.h"

#include <stdio.h>
#include <string.h>

void inkstand_toast_init(struct inkstand_toast *toast) {
    if (toast != NULL) {
        memset(toast, 0, sizeof *toast);
    }
}

/* Moves the oldest waiting notice up to the snackbar. The caller says whether it is dated. */
static void promote(struct inkstand_toast *toast, uint64_t until_ms) {
    snprintf(toast->text, sizeof toast->text, "%s", toast->queue[0]);
    toast->until_ms = until_ms;
    memmove(&toast->queue[0], &toast->queue[1],
            (INKSTAND_TOAST_QUEUE - 1U) * sizeof toast->queue[0]);
    toast->queued--;
    memset(toast->queue[toast->queued], 0, sizeof toast->queue[toast->queued]);
}

/*
 * Takes a notice that cannot be said yet, or says why it need not be.
 *
 * True when the caller has nothing more to do - the notice is queued, or repeats one already on
 * its way. False means the snackbar is free and the caller should put the notice straight up.
 *
 * The repeat test is against what is *showing* and against the newest thing waiting, which is the
 * shape the duplicate actually takes: one event reported twice in a row, rather than the same
 * sentence coming back around after two others. Two identical notices in a row are one notice that
 * stood for eight seconds - a snackbar with a stuck button rather than news.
 */
static bool queue(struct inkstand_toast *toast, const char *text) {
    if (toast->text[0] == '\0') {
        return false; /* nothing is up; say it now */
    }
    /* Compared as it would be stored: a notice too long for the snackbar is kept cut to fit, and
       the same long notice twice would otherwise never match its own stored copy. */
    char stored[INKSTAND_TOAST_TEXT_MAX];
    snprintf(stored, sizeof stored, "%s", text);
    const char *newest = toast->queued > 0U ? toast->queue[toast->queued - 1U] : toast->text;
    if (strcmp(newest, stored) == 0) {
        return true;
    }
    if (toast->queued >= INKSTAND_TOAST_QUEUE) {
        /* Drop the oldest waiting one and close the gap - see the field for why it is that end. */
        memmove(&toast->queue[0], &toast->queue[1],
                (INKSTAND_TOAST_QUEUE - 1U) * sizeof toast->queue[0]);
        toast->queued = INKSTAND_TOAST_QUEUE - 1U;
    }
    snprintf(toast->queue[toast->queued++], INKSTAND_TOAST_TEXT_MAX, "%s", text);
    return true;
}

void inkstand_toast_set(struct inkstand_toast *toast, uint64_t now_ms, const char *text) {
    if (toast == NULL) {
        return;
    }
    if (text == NULL || text[0] == '\0') {
        inkstand_toast_init(toast);
        return;
    }
    snprintf(toast->text, sizeof toast->text, "%s", text);
    toast->until_ms = now_ms + INKSTAND_TOAST_STAND_MS;
}

void inkstand_toast_raise(struct inkstand_toast *toast, const char *text) {
    if (toast == NULL || text == NULL || text[0] == '\0') {
        return;
    }
    snprintf(toast->text, sizeof toast->text, "%s", text);
    toast->until_ms = 0U;
}

void inkstand_toast_post(struct inkstand_toast *toast, uint64_t now_ms, const char *text) {
    if (toast == NULL || text == NULL || text[0] == '\0') {
        return;
    }
    if (queue(toast, text)) {
        return;
    }
    snprintf(toast->text, sizeof toast->text, "%s", text);
    toast->until_ms = now_ms + INKSTAND_TOAST_STAND_MS;
}

/*
 * Only the one showing has been seen, so only the one showing goes. What is waiting stays queued
 * and is put up by date() or the next tick - *after* the press has had its say. Promoted here, a
 * waiting notice would be up for the length of the press and then lost to the notice the press
 * itself raised, which is most presses that dismiss one.
 */
bool inkstand_toast_dismiss(struct inkstand_toast *toast) {
    if (toast == NULL || toast->text[0] == '\0') {
        return false;
    }
    toast->text[0] = '\0';
    toast->until_ms = 0U;
    return true;
}

void inkstand_toast_date(struct inkstand_toast *toast, uint64_t now_ms) {
    if (toast == NULL) {
        return;
    }
    if (toast->text[0] == '\0') {
        /* Nothing showing, and something waiting - a press dismissed a notice and raised none of
           its own. The next one starts standing now. */
        if (toast->queued > 0U) {
            promote(toast, now_ms + INKSTAND_TOAST_STAND_MS);
        }
        return;
    }
    if (toast->until_ms == 0U) {
        toast->until_ms = now_ms + INKSTAND_TOAST_STAND_MS;
    }
}

bool inkstand_toast_tick(struct inkstand_toast *toast, uint64_t now_ms) {
    if (toast == NULL) {
        return false;
    }
    if (toast->text[0] == '\0') {
        /* Nothing showing is not the same as nothing to show: whatever took a notice down without
           dating the snackbar afterwards leaves the queue for the tick, rather than stranded. */
        if (toast->queued == 0U) {
            return false;
        }
        promote(toast, now_ms + INKSTAND_TOAST_STAND_MS);
        return true;
    }
    if (now_ms < toast->until_ms) {
        return false;
    }
    if (toast->queued > 0U) {
        /* The next one takes the snackbar, dated from this tick rather than from whenever it was
           raised: it is starting to stand now, and a deadline the backend has not seen is how it
           tells one notice from the next. */
        promote(toast, now_ms + INKSTAND_TOAST_STAND_MS);
        return true;
    }
    toast->text[0] = '\0';
    toast->until_ms = 0U;
    return true;
}
