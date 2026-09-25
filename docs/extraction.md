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

### 2b. The scene runner - done

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
4. *Done, mesh-client 8b5ff3c.* mesh-client's half: `devtools/ui_capture/main.c` plays its
   scenes on this runner and its copy is deleted.

**Stays behind:** every fixture verb, the invented radio, mesh-client's scenes,
`scripts/ui-capture.sh` and `frames.py`. So do `tab` and `context` for now - both need
mesh-client's screen order and focus ids, and `tab` needs to know that the shoulders mean "the
next tab" - until step 7 gives this side a screen list. `toast` stays until step 5 brings the
overlays down.

### 3. Persistence - done

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

**3a. The field reader and the key table - done.** The two leaves the rest stand on.

- **Moved:** `include/inkstand/persist/fields.h` and `src/persist/fields.c`, from mesh-client's
  `store_fields.c` as it was - it named nothing of the application, so it needed no seam. And
  `include/inkstand/persist/keys.h` and `src/persist/keys.c`, from the half of `store_keys.c`
  that does what any key table does: the brackets a key carries, the exact lookup that refuses a
  malformed one, and the writers with their escape. The seam was made in mesh-client first
  (`store_key_table.{c,h}`): a row is a spelling, its `sizeof` length and its kind, a key is an
  index into a table the caller hands in with 0 reserved for `NONE`, and the formatted writers
  take a `va_list` so the application's printf-like wrapper forwards to them. They are
  `persist/`'s first sources, so `inkstand_core` is a STATIC library now.
- **Tests:** mesh-client's three value cases came as `tests/suites/persist_fields.c`, and its
  key-lookup case as `tests/suites/persist_keys.c`, over a table of the suite's own with the same
  hazards - one name that is a count and its rows, names that prefix each other. The writers'
  cases are new; mesh-client held them only through a whole cache's round trip, which stays
  there.
- **Stayed behind:** `store_keys.def` and every key, the enum it is indexed by,
  `mesh_ui_store_key_in_cache()` (which file a key belongs to is the application's), and a
  one-line wrapper per function so no caller in mesh-client changed.

**3b. The journal - done.** One append-only journal under both logs.

- **Moved:** `include/inkstand/persist/journal.h` and `src/persist/journal.c`. The seam was made
  in mesh-client first, as `store_journal.{c,h}` under both files, and came down in a commit that
  is only a move before the rename. What the two had in common was more than this page guessed:
  not only the append and the compaction but the directory that disables itself rather than
  failing, the ring a reader folds a file into, the rewrite through a temporary, the filtered
  copy the archive's delete streams through, and the trend log's wipe. A subject is a plain word
  the caller spells - `c07`, `n1a2b3c4d` - and a record is the caller's callbacks.
- **Decided on the way:** every question about a file is asked of the stream the append holds
  (the trend log's rule; the archive had stat()ed the name after appending). Compaction stays
  the caller's, cued by the append reporting the file over its cap, because only the caller
  knows where one record ends. The ring counts what it drops for both, though only the archive
  asks. A filter that drops nothing leaves the file alone. Two kernel calls went further down
  than here: `inkwell_file_mkdir()` and `inkwell_file_replace()`, the second because a bare
  `rename()` refused over an existing file on Windows, and both mesh-client's rewrites used one.
- **Tests:** `tests/suites/persist_journal.c`, new. mesh-client held these promises only through
  a whole store's round trip; its archive and trend suites still run there, over this journal,
  and hold what the records mean.
- **Stayed behind:** everything about what a record is - the message codec and the fold of a
  re-delivered message, the delta chain and its restart seam, which conversation a message
  belongs to, the recent ring and the seed, the retention numbers and the suffixes.

**3c. The recently-used list - done.** One list under both of the preferences' lists.

- **Moved:** `include/inkstand/persist/recent.h` and `src/persist/recent.c`. The seam was made in
  mesh-client first, as `store_recent.{c,h}` under `preferences.c`, which kept two lists - the
  peers it had been connected to and the addresses it had reached them at - and wrote each by
  hand. A list is a view over the caller's array and its count, so the record keeps the
  `entries[8]` and count byte it always carried and nothing allocates. What an entry is, when
  two are the same (an address that matches in either case, a transport that is part of the
  name) and how one is spelled in the file are the caller's callbacks.
- **Decided on the way:** a note copies its entry before anything moves, because the natural
  call passes a pointer into the list it reorders - which is what bounds an entry at
  `INKSTAND_RECENT_ENTRY_MAX`. A line is read in file order, not replayed through a note, and an
  entry the parser refuses is skipped rather than ending the line.
- **Tests:** `tests/suites/persist_recent.c`. The order, the bound and the round trip came from
  mesh-client's preference suite; the rest is new. That suite still runs there, over this list,
  and holds what the two lists mean.
- **Stayed behind:** the preferences file itself - its keys, its path, every setting in it, the
  migration of a file from before the lists existed - and which devices and which transports the
  two lists hold.

### 4. The form model and codec - in part

`src/ui/settings/settings_codec.c` (324 lines) is already nearly a leaf: text to value and back
for decimals, numbers and keys, over inkwell's `text.h` and `base64.h`. It comes first and
alone. Then the mechanism half of `settings.c` and `settings_rows.c` (about 5,200 lines
between them): field kinds, steps and tracks, sections and groups, pending edits.

The seam is the field table. `enum mesh_ui_setting_field` and its ~200 rows stay in mesh-client
as a `.def`; `form/` reads a table of descriptors and never an enum it owns.

**Stays behind:** every field, every section, every radio action, and what saving one means.

**4a. The text codec - done.** `form/`'s first sources.

- **Moved:** `include/inkstand/form/codec.h` and `src/form/codec.c`: a decimal held as a scaled
  integer, a 32-bit identifier, and a run of bytes as hex or base64, each parsed and printed side
  by side. The seam was made in mesh-client first, as `form_codec.{c,h}` under
  `settings_codec.c`, because each of the three had a fact of the application baked into it: the
  places a coordinate is held to, the `!` a node number is written with, and the three sizes a
  key is read as hex at. The marker and the sizes are arguments now, and the places always were.
- **Decided on the way:** the codec needs nothing of inkcell and could have been headless, but
  it is filed where the plan put it, in `form/`, because nothing but a form parses what a person
  typed into a row. If a headless program ever wants it, it moves to `persist/` or a `text/` of
  its own without a caller changing.
- **Tests:** `tests/suites/form_codec.c`. The decimal, identifier and key cases came from
  mesh-client's settings suite, at the same widths and with the same refusals; the cases for the
  marker and the sizes are new. That suite still runs there, over this codec, through
  `settings_codec.c`.
- **Stayed behind:** `settings_codec.c` itself, as the wrappers that supply this client's
  spellings - `MESH_UI_COORD_DIGITS` and the coordinate pair, the `!` node number, the key sizes -
  and every `mesh_ui_settings_*` entry point, so no caller in mesh-client changed.

**4b. The number scale and the choice set - done.** The two walks a row makes through its
values, which needed no field id at all.

- **Moved:** `include/inkstand/form/scale.h` and `src/form/scale.c`: a number row's presets
  (`struct inkstand_form_presets` - the values, whether they measure or name, whether a leading
  0 is a word stood aside), the step through them, the track a scale is drawn as, and the
  bitmask walk a choice row makes. The seam was made in mesh-client first, as `form_scale.{c,h}`:
  the four preset columns of its field table became one member, and the macros that fill them
  brace their values, so the table's positional rows did not change.
- **Decided on the way:** the track's 0..1000 is inkcell's `INKCELL_ANIM_ONE`, a meter's fill,
  rather than a number of this area's own - the one thing a track is for is being drawn beside
  one. The header states the contract an all-negative list relies on: the order is uint32_t's.
- **Tests:** `tests/suites/form_scale.c`. The step, track and choice cases came from
  mesh-client's settings suite over preset lists of the suite's own; the cases for a value
  between or past the presets, an all-negative list, a single stop, a repeat and a mask past the
  range are new. mesh-client's cases stay too, because they also hold which of its fields is a
  scale.
- **Stayed behind:** every preset list and which field uses it, the kind check in front of each
  call, and `struct mesh_ui_settings_track`, which the renderer still reads.

**4c. The field descriptor - done.** One row of a form, and the questions every caller asks of
one.

- **Moved:** `include/inkstand/form/field.h` and `src/form/field.c`: `enum inkstand_form_kind`
  (every kind a row can be, from INFO to METER), `struct inkstand_form_field` (label, kind,
  section, limit, enum names, presets, choices, note), and `struct inkstand_form`, which reads
  the application's table through a stride. The seam was made in mesh-client first, as
  `form_field.{c,h}`, in two commits: one that widened every place a field id was kept to 16
  bits, and one that split the descriptor out of the field table.
- **Decided on the way:** the application *extends* a row by embedding the descriptor as its
  first member, so columns that are its alone - a formatter that knows its units, a word for
  what 0 means - sit after it in the same table without inkstand naming them. mesh-client's 195
  positional rows were reordered once, by a script, to put the descriptor's columns first. A
  field id is an index with 0 for no field, and every id past the table resolves to row 0; a
  section is a 16-bit id the form compares and never interprets.
- **Tests:** `tests/suites/form_field.c`, new, over a table of the suite's own built the way an
  application builds one. mesh-client's settings suite still holds what its rows are.
- **Stayed behind:** the field enum and all 195 rows, the sections, the groups of flag rows,
  `key_len_ok()`, the formatters and zero-words, and every `mesh_ui_settings_*` accessor as the
  one-line call into this descriptor.

**Not yet:** pending edits and the row model. Pending edits are about seventy lines of array
work - find, set or append, remove, clear, drop by commit group - over a record whose text buffer
is sized by the application's field list, and bringing them down means describing that record
through a stride, a text offset and a text size: more plumbing than logic. The row model is the
builder primitives under `settings_rows.c`, which want the words a row prints (on, off, empty,
secret) passed in as ids. Both stay in mesh-client until something else needs them, and step 5
went first.

### 5. The overlays - in progress

The toast queue, the confirm dialog, the keyboard session and help, out of `src/ui/nav/nav.c`
(2,893 lines) and `nav_keyboard.c` (598). These are the first pieces of `nav/` and can move
before the stack does, as services the flat nav calls.

**Stays behind:** the seven jobs mesh-client opens the keyboard for, and its emoji pages.

**5a. The snackbar's queue - done.** The one overlay that named nothing but itself.

- **Moved:** `include/inkstand/nav/toast.h` and `src/nav/toast.c`: one notice showing, its
  deadline, and three waiting behind it, as a plain struct an application embeds in its nav - so
  a snapshot is still a copy. Set and raise for a press, which replaces what is showing; post for
  an arrival, which waits its turn; dismiss for a press that takes one down; date for the clock a
  press did not have; and tick. The seam was made in mesh-client first, as `toast.{c,h}` under
  `nav.c`, after a fix in its own commit: a press used to clear the notice and strand the queue
  behind it, which the tick then never walked, so the next arrival jumped ahead of it.
- **Decided on the way:** the toast is view state and lives in `nav/`, not `state/`: posting is a
  direct call the store makes, not an action, until step 6 says otherwise. The limits (64 bytes,
  three waiting, four seconds) are inkstand's constants, not an application's to override - a
  library and an application compiled with different ones would disagree about the struct.
- **Tests:** `tests/suites/nav_toast.c`. The queue case came from mesh-client whole and the
  expiry and dismiss rules sliced out of its navigation case; the undated path is new.
- **Stayed behind:** every sentence a notice says, which press sets one and which arrival posts
  one, the dating after a press in `mesh_ui_store_handle_key()`, the store's dirty flag, and
  `mesh_ui_nav_*_toast` as the one-line calls into this.

**5b. The two-answer dialog - done.** What every go-ahead-or-don't question has in common.

- **Moved:** `include/inkstand/nav/dialog.h` and `src/nav/dialog.c`: `struct inkstand_dialog`
  (open, the cursor, and a subject the dialog carries and never reads), opening on Cancel so a
  repeated press changes nothing, and a key that answers moved, accepted or cancelled - and on an
  answer closes, handing back what the question was about. The walk between the two answers is
  resolved inside the dialog over the frame's focus map, so a row dimmed behind it can never be
  where the cursor goes; before the dialog's first frame, any direction toggles. The seam was made
  in mesh-client first, as `dialog.{c,h}` under its confirm sheet, whose three fields became one
  `struct` on the nav.
- **Decided on the way:** the dialog is the modal and not the question. mesh-client's verify
  sheet has three stages and answers of its own, and takes only the walk; its confirm sheet takes
  all of it and keeps what accepting *means* - the save, the radio action, the cursor a cleared
  slot moves.
- **Tests:** `tests/suites/nav_dialog.c`. Two cases came from mesh-client's capture suite over
  hand-built maps; the layouts, the edge, and every answer a key gives are new.
- **Stayed behind:** every question and its words, the four places that raise one, what each
  accepted answer does, the verify sheet, and this client's focus ids for the answers.

**Next:** help and the keyboard session wait for step 7: both decide which screen to return to,
which is the router's question.
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
