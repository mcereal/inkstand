# AGENTS.md

Contributor guide for inkstand. [`README.md`](README.md) says what the project is and why; this
says how to work in it.

## The one-paragraph version

inkstand is the application layer over [inkwell](https://github.com/mcereal/inkwell) (the
runtime) and [inkcell](https://github.com/mcereal/inkcell) (the UI toolkit): state, persistence,
forms, navigation and lifecycle, in the shape of Elm's architecture. C17, no threads, one loop.
Five areas: `state/` and `persist/` are headless and link inkwell alone; `form/`, `nav/` and
`app/` add inkcell. Arrows point down and `scripts/check-layers.py` holds them there. It is being
extracted from [mesh-client](https://github.com/mcereal/mesh-client), and
[`docs/extraction.md`](docs/extraction.md) is the running map of what comes next. `make test`
before every push.

## Layout

```
include/inkstand/<area>/  the public surface of an area, flat
src/<area>/               the sources; a header and its source always share a filename
third_party/              inkwell and inkcell, as submodules
tests/framework/          the self-registering case runner
tests/suites/<area>_*.c   one file per subject
tests/support/            fixtures a second suite needed
scripts/check-layers.py   the layering rule the compiler cannot see
docs/architecture.md      why the areas are shaped the way they are
docs/extraction.md        what comes over from mesh-client, in what order
```

`include/inkstand/<area>/` is flat and is the interface. How a source is filed under
`src/<area>/` is not part of it: moving a file between groups inside an area is not an API
change. Find a header's source by *filename*, never by path.

## Style

- clang-format 18, config in `.clang-format`. `make format` before pushing; CI checks it.
- `inkstand_` on everything public, `INKSTAND_` on macros and enum members. A symbol carries the
  prefix of whoever owns it, so a name that came from mesh-client is renamed on the way in
  rather than keeping its old spelling: `mesh_ui_controller` is `inkstand_controller`.
- Every public header is `#pragma once`, wrapped in `extern "C"`, and says in prose *why* it
  works the way it does. The reasoning is the valuable part; a signature can be read off the
  line below it.
- Functions return `0` or a negative `errno` for status, and the quantity itself when the
  answer is a quantity - inkwell's rule, for inkwell's reasons.
- Tables are data. What an application declares - screens, actions, fields, cache keys - is a
  `static const` array or an X-macro `.def` file, never a switch statement inkstand has to grow.
  A `.def` is not a header: no guard, and `make format` does not touch it.

## Rules that compile fine when broken

- **`state/` and `persist/` never include inkcell.** They are what a headless program links.
  `check-layers.py` names the line; the `headless` CI job fails the build.
- **No threads.** A store wakes the loop through `inkwell/runtime/wake.h`; it never signals
  anything.
- **No kernel call above inkwell.** Register `INKWELL_LOOP_IN`/`_OUT`, never `EPOLLIN`, and take
  a timer, a wake or a socket from inkwell. The macOS CI job is the thing that notices.
- **No application vocabulary.** Not in code, not in comments. "A subject", "a record", "a
  peer" - never "a node", "a channel", "the radio".
- **Nothing on the press-to-frame path allocates.** The application sizes its state, its stack
  frames and its queues at compile time. A `malloc` in `nav/` or `state/` is a bug unless it is
  in an init function and the header says so.
- **A word a user reads is a string id.** inkstand reports a reason or an id and the application
  turns it into a sentence - inkwell's `net/reason.h` rule. A toast carries text the application
  already translated.
- **A new area under `src/` needs an entry in `ALLOWED`** in `scripts/check-layers.py`. Adding
  one is a decision about the shape of the stack.
- **`#include <mesh/...>` is a build failure.** inkstand is below every application.

## Extracting something from mesh-client

The order and the method are inkwell's, because they worked there:

1. **Check what it actually depends on.** `grep -h '#include "' <file>` over the source **and
   its header**, then what the source *calls* across the line. `controller.c` includes four
   mesh-client headers; the move is not ready until each is a seam.
2. **Make the seam in mesh-client first, in its own commit.** Change the component so it stops
   naming the application - a callback, a size, a table handed in - with mesh-client's tests
   still in place. Then move it in a commit that is only a move. A rename folded into a
   relocation hid 26 wrong names in inkwell's MQTT extraction; do not repeat that.
3. **Move the tests with it.** A component that arrives without the cases that held it is a
   downgrade however clean the diff looks.
4. **Rename on the way in.** `mesh_ui_*` becomes `inkstand_*`, and a name shaped by the
   application gets the better name now.
5. **Generalise the prose, do not delete it.** mesh-client's comments explain real decisions -
   why a toast queue drops the oldest *waiting* notice, why a route is derived rather than
   flagged. Rewrite "a conversation" as "a subject" and keep the argument.
6. **Leave the seam in the application.** The general form comes here and the two-line wrapper
   stays there.

An extraction PR says what it moved, from where, and what stayed behind - the last of those is
the part a reviewer cannot get from the diff.

## Tests

```bash
make test                                   # the whole verify step
make headless                               # the headless half alone
./build/tests/inkstand_tests --list
./build/tests/inkstand_tests --filter store # substring match
./build/tests/inkstand_tests --suite state
```

Cases register themselves from constructors, so adding one is a single `INKSTAND_TEST_CASE` in a
single file. A new *suite file* goes in `tests/CMakeLists.txt` - in the core list if it is about
`state/` or `persist/`, so it runs on the headless job too, and in the UI list otherwise.

A helper used by one suite stays `static` in it and moves to `tests/support/` when a second
suite needs it.

## Pull requests

- `make test` green, `make format` clean.
- Conventional Commits (`feat:`, `fix:`, `refactor:`, `docs:`, `test:`, `chore:`).
- An extraction PR comes with its mesh-client half: the PR there that switches to the moved
  component and deletes the old copy.
