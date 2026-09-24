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

### 1. The controller and the frame scheduler

`src/ui/nav/controller.c` (435 lines) drains the store into a snapshot, hands it to a backend's
`present()`, and arms a ~33 ms frame timer only while the backend says something is still
moving - so a screen sitting still costs no wake-ups. None of that is Meshtastic.

What it names across the line: `mesh/ui/store.h` (the snapshot type), `mesh/ui/nav.h`,
`mesh/ui/focus.h` and `mesh/ui/commands.h`. The seam is the snapshot: the controller copies and
presents one without reading it, so it needs a size and a drain callback, not the type.

**Stays behind:** the snapshot's contents, and the action handler's body.

### 2. The control socket and the scene runner

`src/app/app_control.c` (509 lines) is the Unix socket that lets a developer or an agent press
keys by name, wait, and bring back a frame as a picture - what mesh-client's `make ui-drive`
stands on. `devtools/ui_capture/` (about 3,200 lines) drives the same presses through a script
and renders every frame off-screen. Every application on this stack wants both, on day one.

What it names across the line: `mesh/ui/controller.h` (step 1 removes that) and
`mesh/ui/route.h`, for the `screen` command's answer. The seam is a callback returning the
current screen's ASCII id - which the screen stack in step 7 answers for free.

**Stays behind:** mesh-client's scenes, and the socket path and environment variable names.

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
