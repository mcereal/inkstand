# What comes over from mesh-client

inkstand exists because mesh-client's `src/ui/{store,nav,settings}` and `src/app/` - about
27,000 lines - turned out to be two things in one place: an application that knows what a node
and a channel are, and a framework that knows what a store, a screen and a form are. This page
is the running account of which parts of the second are still on the wrong side of the line.

It is a map, not a promise. A row is a candidate with its evidence, and the evidence is what
decides whether it moves.

## The test a candidate has to pass

inkwell's three questions, in its order:

1. **Does it name anything from the application?** `grep -h '#include "' <file>` over the
   source **and its header**, then what the source actually *calls* across the line. A header
   that includes two more is how a candidate that looks clean is not.
2. **Is there a seam, or only a wrapper?** The general form comes here and a two-line wrapper
   stays behind. Dragging the application's constant down and calling it general is the failure.
3. **Do its tests come with it?** mesh-client's suites under `tests/suites/` hold most of these
   components; the cases that belong to one come with it and the rest stay.

## In order

Easiest and most useful first; most entangled last. Line counts are mesh-client's, at the time
this page was written.

### 1. The frame scheduler - done

**Moved:** `include/inkstand/nav/frame_scheduler.h` and `src/nav/frame_scheduler.c`, from the
presenting half of mesh-client's `src/ui/nav/controller.c`. It watches a store's wake, drains
into a snapshot the application owns, presents it, and arms a frame timer only while the backend
says something is still moving - so a screen sitting still costs no wake-ups. The seam turned out
to be what this page guessed: a descriptor, a drain callback and a snapshot pointer, plus an
optional "nothing changed" hook for the timer's frames and a "publish once" hook for the first
frame. The frame interval is the application's, passed in. Its cases came with it as
`tests/suites/nav_frame_scheduler.c`.

**Stayed behind:** the controller itself. Since this page was first written it had grown a
command dispatcher, clicks, a context menu and desktop shortcuts, all resolved against
mesh-client's snapshot and its command table - so the half that decides what a press *means* is
the application's, and it now stands on the scheduler rather than beside it. It comes down when
step 7 gives a screen its own `on_key`, not before.

### 2. The control socket - done

**Moved:** `include/inkstand/app/control.h` and `src/app/control.c`, from mesh-client's
`src/app/app_control.c`: the Unix socket that lets a developer or an agent press keys by name,
wait, and bring back a frame as a picture - what mesh-client's `make ui-drive` stands on. Both
ends came: the listening end on the loop, and the blocking sending end a program's "send these
commands" flag runs. Windows builds `control_unavailable.c`, which refuses every call.

The seam is `struct inkstand_control_host`, and it is what this page guessed: the frame
scheduler from step 1 answers `settled()` and `frame()` for `shot`, a `press` callback is `key`,
and an optional `screen` callback returns an ASCII id. Its cases came with it as
`tests/suites/app_control.c`, over a fake host, with new ones for `wait`, a second connection,
and the socket removing only its own file. The fake store moved to `tests/support/` when this
second suite needed it.

**Stayed behind:** the flag and environment variable that name the socket, the scripts that
clear a stale one, and the test that the default config opens none - all mesh-client's.

### 2b. The scene runner - designed, in progress

`devtools/ui_capture/main.c` (3,184 lines) drives presses through a script and renders every
frame off-screen. It is the control socket's offline twin: the same press and the same screen
id, on a clock the script names instead of the real one, into memory instead of onto a panel.

**What the evidence says.** This page first read it as a handful of generic verbs and a long
list of fixtures, and by *verb* that is right: of about forty, seven are generic and the rest
build Meshtastic state (`airtime`, `firmware-install`, `broker`, `verify`, ...). By *use* it is
the other way round. Of the lines mesh-client's 74 scenes contain, `hold` and `key` are about
1,100, `tab` and the setup verbs (`scene`, `scale`, `delay`, `theme`, `clock`, `frame`) about
340, and every fixture together about 200. The code splits the same way: about 400 lines of
engine - the tokenizer, emit, settle, hold, the frame manifest - over roughly 2,700 of invented
radio and fixture verbs. And the off-screen render loop this page expected to move is already
down: inkcell's `inkcell_capture_*` owns the clock, `animating()`, the render and the PPM. What
is left is policy - when a frame is emitted and how long it stays up.

**The seam** is the control socket's, with the frame scheduler's callback types:

- `struct inkstand_scene_host` - the capture, the snapshot, `drain` and `refresh` (the
  `inkstand_frame_*_fn` types from step 1), `press(key, page_rows)`, `tick(now_ms)` for the
  housekeeping a loop turn does, `screen()` as the control host has it, a table of named seeds
  for `scene NAME`, a `started` hook, and the application's verb table.
- `struct inkstand_scene_verb` - a name, a flag or two (setup-only; emits its own frames), and
  `int run(scene, args, userdata)`. The fixtures become rows of this; helpers to take a word, the
  rest of the line and a bounded number, and to emit and settle, are public so a row is as short
  as the branch it replaces.
- `struct inkstand_scene_sink` - where frames go. `frame(index, capture)` when one is drawn, and
  `delay(index, ms)` once its delay is final. The file sink writes the PPMs and `frames.txt`; a
  test's sink counts.

Four rules it holds, each a change from `main.c`:

1. **No `exit()`, and no files unless the sink writes them.** A verb returns 0 or a negative
   errno and the runner records the line it failed on. A scene then runs in-process, which is
   the first piece of the screen test harness in [architecture](architecture.md#what-is-new-rather-than-moved).
   Its diagnostics are ASCII for a developer, like the control socket's `error busy` - not a
   word a user reads.
2. **"Every command emits one frame" is the runner's rule, not each verb's.** 33 fixture
   branches end in a hand-written emit; here the runner emits and settles after any verb that
   does not say otherwise, so a new fixture cannot forget.
3. **Nothing grows.** `hold` only ever lengthens the frame just emitted, so one pending delay is
   all the state the manifest needs: frame N's delay is final when frame N+1 is drawn or the
   script ends. That is what `delay()` on the sink is for, and it retires `main.c`'s `realloc`.
4. **The numbers are the application's.** The frame interval, the settle cap, the default
   delay and the geometry are passed in, as the frame scheduler's interval is.

**The second application.** The verb-table half of the header is marked unstable until the
step-4 application uses it. The engine's second user is this repository's own suite, running
scenes in-process over the fake host in `tests/support/`, and an `expect screen ID` verb over
`screen()` is what turns a scene into a test.

**In order:**

1. *Done, mesh-client #317.* A baseline of every scene's frames by delay and pixel hash, with the
   wall clock pinned - without the pin, 18 scenes differ between two runs of the same binary.
   Then the seam, in its own commit: `devtools/ui_capture/scene.{c,h}` is the runner and names
   nothing of mesh-client's, and `main.c` is the host, 37 verb rows and two seeds. All 78
   scenes were byte-identical across it. Two fixes followed as their own commits, each found by
   making a rule the runner's: 26 verbs had emitted without settling, and a second `syncing`
   verb had never been reachable.
2. *Done.* The runner moved here as `include/inkstand/app/scene.h` and `src/app/scene.c` in a
   commit that is only a move, then renamed `uicap_scene_*` to `inkstand_scene_*` in the next.
   `expect screen ID` came with it, over the host's `screen()`.
3. *Done.* Its cases, written here as `tests/suites/app_scene.c`, over a counter for a host and
   an `inkcell_fb_app` that says how long it is still moving. `main.c` had none of its own -
   mesh-client's `ui_capture` suite tests inkcell's capture, not the script.
4. mesh-client's half, switching `devtools/ui_capture/main.c` to this runner and deleting its
   copy.

**Stays behind:** every fixture verb, the invented radio, mesh-client's scenes,
`scripts/ui-capture.sh` and `frames.py`. So do `tab` and `context` for now - both need
mesh-client's screen order and focus ids, and `tab` needs to know that the shoulders mean "the
next tab" - until step 7 gives this side a screen list. `toast` stays until step 5 brings the
overlays down.

### 3. Persistence

| File | Lines | What comes | What stays |
|---|---|---|---|
| `src/ui/store/store_keys.c` + `store_keys.def` | 242 | The X-macro key table mechanism and its rules | The keys |
| `src/ui/store/store_fields.c` | 156 | Reading and writing a record's fields in order | The field lists |
| `src/ui/store/store_archive.c` | 894 | An append-only journal per subject, bounded, with compaction | The message codec, conversation routing |
| `src/ui/store/store_trends.c` | 704 | The same, for time series | Which readings, which nodes |
| `src/ui/store/preferences.c` | 466 | A bounded most-recently-used list, persisted | Which devices, which transports |

inkwell's `base/record_file.h` is already the line reader and writer under the first four. The
archive and the trends log each built a journal separately; they become one.

What they name across the line: `mesh/utils/file.h`, `mesh/ui/nav.h` (the archive routes by
conversation) and each other's key tables.

### 4. The form model and codec

`src/ui/settings/settings_codec.c` (324 lines) is already nearly a leaf: text to value and back
for decimals, numbers and keys, over inkwell's `text.h` and `base64.h`. It comes first and
alone. Then the mechanism half of `settings.c` and `settings_rows.c` (about 5,200 lines
between them): field kinds, steps and tracks, sections and groups, pending edits.

The seam is the field table. `enum mesh_ui_setting_field` and its ~200 rows stay in mesh-client
as a `.def`; `form/` reads a table of descriptors and never an enum it owns.

**Stays behind:** every field, every section, every radio action, and what saving one means.

### 5. The overlays

The toast queue, the confirm dialog, the keyboard session and help, out of `src/ui/nav/nav.c`
(2,893 lines) and `nav_keyboard.c` (598). These are the first pieces of `nav/` and can move
before the stack does, as services the flat nav calls.

**Stays behind:** the seven jobs mesh-client opens the keyboard for, and its emoji pages.

### 6. The store, and effects

The publish-and-wake mechanism of `src/ui/store/store.c` (1,543 lines) is generic; its records
are not. The larger half of this step is in mesh-client: turning `src/app/app_actions.c`
(2,025 lines) from a handler that does I/O into an update that returns effects. See
[architecture decision 2](architecture.md#2-update-is-pure-effects-are-requests).

**Stays behind:** every record, and `src/app/app_publish.c` (2,781 lines), which is the
translation from a Meshtastic session into those records and is entirely the application's.

### 7. The router

`src/ui/nav/route.c` (445 lines) and the navigation model in `nav.c`, reshaped from a flat
struct into a screen stack. Done in mesh-client first, against its `ui_nav_*` and `ui_route`
suites, and moved only once they pass. See
[architecture decision 3](architecture.md#3-a-screen-stack-not-a-flat-nav).

**Stays behind:** every screen.

### Optional, any time: the self-updater

`src/core/update/updater.c` (958 lines) checks a release, downloads it, verifies its SHA-256 and
swaps the binary. It names mesh-client's catalog (`mesh/i18n/strings.h`,
`mesh/i18n/net_reason.h`) and its version header; the seam is a reason code in place of a string
id, which is the move inkwell already made for the network stack. It would land as
`app/update`.

## Open questions

- **Does the stack keep the route derived?** mesh-client's argument against a stored flag is
  that it drifts. A stack is stored, but it is the *only* store, so nothing can disagree with
  it. The question is settled by step 7's tests, not by this page.
- **How much of `form/` is really headless?** A field's label is a string id, and the catalog
  that owns string ids is inkcell's - which is the only reason `form/` sits on the UI side. If
  inkcell's catalog ever comes down into inkwell (inkwell's `docs/extraction.md` explains why it
  has not), `form/` moves to the headless half with it.
- **What is the second application?** Nothing here is proven general until two programs use it.
  A small one - a list, a detail screen and a settings page over some other device - should be
  built alongside step 4, and a headless one alongside step 6, before anything here is called
  stable.

## What must not come here

- **Anything that names the application's domain**, in code or in a comment.
- **A word a user reads.** A string id, a reason code - the application says the sentence.
- **A widget.** That is inkcell's.
- **A threaded anything.**
- **A policy dressed as a mechanism.** A retention count, a toast duration, a frame rate: the
  mechanism comes here and the number is the application's, passed in.
