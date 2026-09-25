# Architecture

How inkstand is meant to be shaped, and why. This is the design the extractions are aimed at;
where one of them proves a decision here wrong, this page changes with it.

## The loop, in one picture

```
            press                               effect result
              |                                       |
              v                                       v
  inkcell_input -> nav (screen stack) -> action -> update(state, action)
                                                      |            |
                                                      |            +-> effects -> the application
                                                      v                          runs them on
                                            store (live state)                   the loop
                                                      |
                                               publish + wake
                                                      |
                                                      v
                             controller: drain -> snapshot -> screen draws it -> frame
```

This is Elm's architecture - Redux's, if that is the one you know. A press is resolved to an
*action*, an *update* decides what changes, and a screen draws from a *snapshot* it cannot
edit. mesh-client already works this way; what it does not have is the framework half separated
from the Meshtastic half, which is the whole of what this repository is for.

## Six decisions

### 1. The application owns the nouns; inkstand owns the plumbing

inkstand never learns what a record *is*. The application declares its state, its actions, its
screens, its form fields and its cache keys; inkstand publishes, schedules, routes, persists and
recovers. In C the declaring is done the way mesh-client already does it for strings and cache
keys - **X-macro `.def` files and `static const` tables** - so adding a screen or a field is
adding a row, and the same list expands into the enum, the name table and the dispatch table
without the three drifting apart.

The alternative - inkstand holding a `void *` and a size and never seeing a type - is what a
store has to do internally anyway, and is fine *under* the tables. It is not a substitute for
them: a framework whose whole interface is `void *` is one whose mistakes all arrive at run time.

### 2. Update is pure; effects are requests

In mesh-client a press becomes a `struct mesh_ui_action`, and the handler it reaches
(`mesh_app_on_ui_action`) does the work - opens a connection, writes a setting, sends a message.
That works, and it means every decision the application makes is tangled with the I/O that
follows it.

inkstand splits them. An update takes the state and an action and returns the new state and a
list of *effects*: "connect to this", "write that", "start this timer". The application runs
the effects on the loop and feeds each result back as another action. This is Elm's `Cmd`, or a
Redux middleware, without anything clever - an effect is a tagged struct, and the runner is a
`switch` the application owns.

What it buys:

- **Every decision is testable without a device.** Hand the update a state and an action, look
  at the state and the effects. No mock transport.
- **A session can be replayed.** If the state is a function of the actions, the last few
  hundred actions *are* the bug report. See [recording](#what-is-new-rather-than-moved).
- **The effects are a list somebody can read.** Today the answer to "what can this client do
  to a radio" is spread across a 2,000-line action handler.

What it costs: mesh-client's action handler has to be turned inside out, and that is the
largest single change the extraction asks of it. It is step 6 of seven for that reason.

### 3. A screen stack, not a flat nav

mesh-client's `struct mesh_ui_nav` holds every screen's state side by side - a cursor per tab,
`thread_open`, `compose_open`, `keyboard_open`, the picker, the armed deletes - and `route.h`
reads those flags back to work out where the reader is. Its header explains why the route is
*derived* rather than stored: eleven places open a level and nine close one, and a stored "this
was a push" flag is a second opinion that one of them will eventually forget to update.

That argument is right, and a stack answers it from the other side: **the stack is the only
opinion**. Opening a screen is a push, backing out is a pop, and depth, the back arrow and the
direction a slide goes are all read off the stack rather than off a set of booleans. Each
screen is a table entry:

```c
struct inkstand_screen {
    const char *id;         /* ASCII and untranslated: scenes, crash reports, the control socket */
    size_t state_size;      /* this screen's own state, held in its stack frame */
    void (*enter)(void *state, const void *arg);
    bool (*on_key)(void *state, const struct inkstand_context *context, enum inkcell_key key);
    void (*draw)(const void *state, const struct inkstand_view *view);
    const struct inkstand_hint *hints;   /* (button, string id) pairs, as actions.c has them */
    inkcell_str_id help;
};
```

A screen's cursor, its draft and its armed press live in its own frame, so a new screen adds
state without widening a struct every other screen shares. Frames are fixed-size and the stack
is bounded, so none of this allocates.

This is the riskiest decision here, because it is a change to how mesh-client's navigation
*works* rather than a file move. It is done inside mesh-client first, against mesh-client's own
nav suites, and only moved once those pass.

### 4. Overlays are services

The toast queue, the confirm dialog, the keyboard session, the help sheet and the "press twice
to confirm" arming are each written once in mesh-client and used by many screens. Here they
are services any screen asks for - `inkstand_toast_post()`, `inkstand_confirm_raise()` - with
their rules kept: a full toast queue drops the oldest *waiting* notice, never the newest, because
a backlog is only worth keeping while it is still news; an armed press names the *subject* it
was armed on rather than the row, because rows re-sort under the cursor.

### 5. A form is a table and a codec

mesh-client's settings model is about 5,500 lines, and most of it is mechanism: a field has a
kind (toggle, choice, number, text, decimal, key), a step and a track for numbers, a maximum
for text, a section and a group; edits are held as pending and committed as one action; a
value round-trips through text by a codec that must parse exactly what it prints. The ~200
Meshtastic fields are data over that mechanism. The mechanism comes here as `form/`; the fields
stay behind as a `.def`. The codec came first, as `form/codec.h`: the parse and the print for
a decimal, an identifier and a run of bytes, with the spellings that are an application's own -
a marker, the sizes a key is read as hex at - passed in rather than known.

### 6. Persistence has two shapes

- **A cache**: one file of typed lines, read at start and rewritten on change. mesh-client's
  `store_keys.def` already states the rules that make one survive upgrades - *a key is forever*,
  *widen by adding a key, never by adding a field*, *one key per group that can arrive on its
  own* - and those become this framework's schema rules, over inkwell's
  `base/record_file.h`.
- **A journal**: an append-only log per subject, which is what `store_archive.c` (a
  conversation's history past the in-memory ring) and `store_trends.c` (a node's readings past
  one run) each built separately, and now share as `persist/journal.h`.

Beside both sits a smaller thing, **a recently-used list** (`persist/recent.h`): the handful of
things a program was last used with, newest first, bounded, and kept as one line of a settings
file - the same move-to-front written twice over in mesh-client's preferences.

Together they are roughly what `localStorage` and a small IndexedDB are to a web page, sized
for an SD card.

## What is new rather than moved

These do not exist in mesh-client, and each is here because the decisions above make it cheap:

- **Recording and replay.** Because update is pure, the last N actions can be kept in a ring and
  written into inkwell's crash report beside where the reader was standing. Replaying them into
  a fresh store reproduces the state that crashed.
- **Selectors with generations.** mesh-client's `docs/ui.md` notes that anything holding a
  snapshot rebuilds when any record in it changes. Each slice of state carries a generation
  counter, and a selector - a filtered list, a row count - recomputes only when the generations
  it read have moved. This is what `reselect` is to Redux.
- **Lifecycle hooks.** Start, suspend, resume, quit. A handheld sleeps and wakes, and today each
  application notices that on its own.
- **A test harness for screens.** Given a state and a sequence of keys, assert the route, the
  actions and - through inkcell's headless backend - the frame. mesh-client's `ui_capture`
  scenes, with assertions.

## What inkstand is not

- **Not a widget set.** Buttons, lists and cards are inkcell's. A screen here decides *what* to
  draw and asks inkcell to draw it.
- **Not a platform packager.** A NextUI `.pak`, a `launch.sh`, a store manifest - those are one
  platform's, and belong in tooling of their own if a second application ever wants them.
- **Not a scheduler.** One loop, inkwell's. Effects run on it; nothing here queues work across
  threads or frames.
