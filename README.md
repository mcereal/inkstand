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

## Status

**Early: two components, and the skeleton around them.** The build, the layering rule, the test
runner and CI are here, and so are the first pieces to come down from mesh-client: the frame
scheduler in `nav/`, which draws only while something is moving, and the control socket in
`app/`, which lets a developer or an agent press keys by name and bring back a picture of the
frame. What arrives next, from where and in what order is
[`docs/extraction.md`](docs/extraction.md), and why it is shaped the way it is is
[`docs/architecture.md`](docs/architecture.md). The table below describes the areas the code is
landing in; most of them are still empty.

## Why it exists

It is the third cut of [mesh-client](https://github.com/mcereal/mesh-client), a Meshtastic
client that began on the TrimUI Brick. The first cut took out everything that was never about
Meshtastic and was about drawing - that is inkcell. The second took out everything that was
never about Meshtastic and never about drawing either - that is inkwell.

What was left behind is about 27,000 lines under mesh-client's `src/ui/{store,nav,settings}` and
`src/app/`, and a good share of it is not about Meshtastic at all. A store that publishes a
snapshot and wakes the loop, a controller that draws only while something moves, a toast queue,
a confirm dialog, a settings model with fields and steps and pending edits, a key table for a
cache that must outlive every build that wrote it, a control socket that lets an agent press
keys and bring back a picture - any second application on this stack would write every one of
those again. That is the evidence for a third layer, and the reason not to build it until
there was a first application to take it from.

The pattern is not new. It is Elm's architecture, or Redux's: state in one place, a press
becomes an action, an update decides what changes, and a view is drawn from a snapshot it
cannot edit. What is particular here is doing it in C17 with fixed memory, no threads and one
loop - and keeping the half that does not draw usable by a program that never will.

## What will be in it

| Area | What it owns | Needs |
|---|---|---|
| `state/` | The store, the snapshot and its wake, actions, effects, selectors that recompute only when their input moved | inkwell |
| `persist/` | A typed cache whose keys are forever, append-only journals per subject, a most-recently-used list | inkwell |
| `form/` | Fields by kind - toggle, choice, number with a step, text with a limit, decimal - sections, groups, pending edits committed as one action, the text codec | inkwell, inkcell |
| `nav/` | The screen stack, the overlays every screen shares (toast, confirm, keyboard, help), the frame scheduler | inkwell, inkcell |
| `app/` | Lifecycle, the composition of the rest, the control socket, the self-updater | inkwell, inkcell |

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
