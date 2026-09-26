# inkstand

An application framework for small, single-threaded C programs: the part of a program that is
neither talking to a device nor drawing a frame, but deciding - what the program knows, where
the reader is, what a press means, and what gets remembered.

```
   application          mesh-client, and whatever comes next
        |
     inkstand          state, persistence, navigation, forms, the app's lifecycle
        |       \
        |      inkcell  theme, fonts, layout, widgets, focus - a framebuffer or a window
        |       /
     inkwell           loop, signals, clock, log, BLE, serial, net, TLS, codecs
        |
   the kernel
```

[inkwell](https://github.com/mcereal/inkwell) is the runtime and
[inkcell](https://github.com/mcereal/inkcell) the UI toolkit; inkstand is what an application
built on both keeps writing for itself. An inkstand is the tray on a writing desk that holds
the well and the pens: it makes no ink and draws no line, it holds the pieces in place so you
can write.

It is being extracted from [mesh-client](https://github.com/mcereal/mesh-client), a Meshtastic
client for the TrimUI Brick: the parts of its store, navigation, settings and app glue that any
second application on this stack would otherwise write again.

The pattern is Elm's architecture, or Redux's: state in one place, a press becomes an action, an
update decides what changes, and a view is drawn from a snapshot it cannot edit. What is
particular here is doing it in C17 with fixed memory, no threads and one loop - and keeping the
half that does not draw usable by a program that never will. How the areas are meant to fit
together, and why, is [`docs/architecture.md`](docs/architecture.md).

## What is in it

**Early.** The areas below are where the code lives; not all of them have code yet.

| Area | What is there | Needs |
|---|---|---|
| `state/` | The store, the snapshot and its wake, actions and effects - designed in `docs/architecture.md`, no code yet | inkwell |
| `persist/` | `keys.h` and `fields.h` (a typed cache whose keys are forever), `journal.h` (an append-only log per subject), `recent.h` (a bounded most-recently-used list) | inkwell |
| `form/` | `codec.h` (text to value and back), `scale.h` (a number row's presets and a choice row's walk), `field.h` (one row of a form, read from the application's table) | inkwell, inkcell |
| `nav/` | `frame_scheduler.h` (draws only while something moves), `toast.h` (the snackbar's queue), `dialog.h` (the two-answer modal) | inkwell, inkcell |
| `app/` | `control.h` (a socket to press keys by name and bring back a frame), `scene.h` (plays a scripted sequence of presses) | inkwell, inkcell |

**`state/` and `persist/` are headless.** They link inkwell and nothing else, so a daemon, a
bridge or a CLI can keep a store and a journal without a toolkit in its link line.
`INKSTAND_WITH_UI=OFF` builds that half alone, and CI builds it that way on every pull request.

## The rules it is built on

These are authoring rules - breaking one compiles and looks fine.

- **The areas point one way.** `scripts/check-layers.py` holds the table of which area may
  include which, and `make test` runs it. `state/` and `persist/` are leaves; `app/` is the one
  area that sees everything, because composing them is what it is for.
- **The headless half never names inkcell.** Not in a header, not in a source.
- **No application vocabulary.** Not in code, not in comments. Say "a subject", "a peer", "a
  record". The day a comment in here says "the radio" is the day this stopped being a framework.
- **No threads.** Everything is inkwell's one loop. A store wakes it; it does not signal a
  thread.
- **The framework owns the plumbing; the application owns the nouns.** Which records exist,
  which screens, which fields and which keys are the application's, and they arrive as table
  rows - `.def` files and `static const` arrays - not as code inkstand has to be taught.
- **Fixed memory where a device lives.** An application says how big its state and its stack
  are; nothing on the path from a press to a frame allocates.

## Building

```bash
git submodule update --init --recursive   # inkwell + inkcell; Mbed TLS nests under inkwell
make test        # Debug build + ctest - the default verify step, and what CI runs
make headless    # state/ and persist/ alone, with no inkcell configured
make format      # clang-format all tracked .c/.h (needs clang-format 18)
```

On macOS the Mbed TLS generator under inkwell wants a Python with `jinja2` and `jsonschema`;
point CMake at one with `-DPython3_EXECUTABLE=...` if the one on `PATH` does not have them.

Sanitizers: `cmake -S . -B build -DINKSTAND_ENABLE_ASAN=ON -DINKSTAND_ENABLE_UBSAN=ON`.

## Using it

Vendor it as a submodule and `add_subdirectory()` it - **after** your own inkwell and inkcell,
so the whole tree builds one of each:

```cmake
add_subdirectory(third_party/inkwell)
add_subdirectory(third_party/inkcell)
add_subdirectory(third_party/inkstand)
target_link_libraries(your_app PRIVATE inkstand::inkstand)   # or inkstand::core, headless
```

Each of the three only brings in a dependency when no target of that name exists yet, so the
first one added wins and nothing is built twice.

## Licence

MIT - see [`LICENSE`](LICENSE).
