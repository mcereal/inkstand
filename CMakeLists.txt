# inkstand is built two ways and has to be right in both: on its own, where it owns the warning
# flags and builds its tests, and as an add_subdirectory() of an application, where it must not
# reach into the parent's targets or install anything. INKSTAND_IS_TOP_LEVEL is what tells them
# apart - CMAKE_SOURCE_DIR equals this directory only in the first case.
cmake_minimum_required(VERSION 3.18)
project(inkstand VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(INKSTAND_IS_TOP_LEVEL ON)
else()
    set(INKSTAND_IS_TOP_LEVEL OFF)
endif()

option(INKSTAND_BUILD_TESTS "Build inkstand's own test binary" ${INKSTAND_IS_TOP_LEVEL})
option(INKSTAND_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(INKSTAND_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

# The half of inkstand that draws - nav/, form/, app/ - and the inkcell it stands on. OFF builds
# the headless half alone (state/, persist/) over inkwell, which is what a daemon or a CLI links,
# and CI builds it that way so the claim that the headless half needs no toolkit is checked
# rather than remembered. The same arrangement inkwell has for Mbed TLS and inkcell for SDL2.
option(INKSTAND_WITH_UI "Build the areas that draw, over inkcell" ON)

# inkwell first, then inkcell, and each only when nothing has brought one in already. An
# application pins one copy of each for the whole tree: it adds its own inkwell and inkcell
# *before* this directory, and these guards then find the targets and step aside. inkcell does
# the same for the inkwell it carries, so however deep the submodules nest, one of each builds.
if(NOT TARGET inkwell)
    add_subdirectory(third_party/inkwell)
endif()
if(INKSTAND_WITH_UI AND NOT TARGET inkcell)
    add_subdirectory(third_party/inkcell)
endif()

# The same INTERFACE-target trick inkwell and inkcell use: -fsanitize=address has to reach the
# compile *and* the link, and a variable holding one of them can be applied to a new target
# without the other. Linked PRIVATE - this is how inkstand compiles, not something a consumer
# inherits.
add_library(inkstand_options INTERFACE)
target_compile_options(inkstand_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wcast-align
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wformat=2
)

if(INKSTAND_ENABLE_ASAN)
    target_compile_options(inkstand_options INTERFACE -fsanitize=address)
    target_link_options(inkstand_options INTERFACE -fsanitize=address)
endif()

# -fno-sanitize-recover because UBSan otherwise prints a diagnostic and carries on, so the test
# that found the undefined behaviour still passes and CI still goes green.
if(INKSTAND_ENABLE_UBSAN)
    target_compile_options(inkstand_options INTERFACE
                           -fsanitize=undefined -fno-sanitize-recover=undefined)
    target_link_options(inkstand_options INTERFACE -fsanitize=undefined)
endif()

# Two libraries, because there are two kinds of consumer.
#
# inkstand_core is state/ and persist/: the store, the actions and effects, the typed cache and
# the journals. It links inkwell and nothing else, so a program with nothing to draw uses it
# without a toolkit in its link line. inkstand is the rest - nav/, form/, app/ - and adds inkcell.
add_library(inkstand_core STATIC
    # persist/ - the records an application keeps on disk
    src/persist/fields.c
    src/persist/journal.c
    src/persist/keys.c
    src/persist/recent.c
)
target_include_directories(inkstand_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
# PUBLIC: a header hands out inkwell's printf archetype, and an application that writes a record
# file reads it back with inkwell's record_file.h.
target_link_libraries(inkstand_core
    PUBLIC inkwell::inkwell
    PRIVATE inkstand_options
)
add_library(inkstand::core ALIAS inkstand_core)

if(INKSTAND_WITH_UI)
    # A platform gap refuses rather than disappearing - inkwell's rule. The control socket is a
    # Unix socket, so Windows builds the file that says so to every caller.
    if(WIN32)
        set(INKSTAND_CONTROL_SOURCE src/app/control_unavailable.c)
    else()
        set(INKSTAND_CONTROL_SOURCE src/app/control.c)
    endif()

    # Grouped by area, as inkwell's sources are.
    add_library(inkstand STATIC
        # form/ - fields, and the text a person types into one
        src/form/codec.c
        src/form/field.c
        src/form/scale.c
        # nav/ - where the reader is, and when a frame is drawn
        src/nav/frame_scheduler.c
        src/nav/toast.c
        # app/ - the composition of the rest, and the socket that drives it from outside
        ${INKSTAND_CONTROL_SOURCE}
        src/app/scene.c
    )
    # PUBLIC: the headers hand out inkwell and inkcell types - an inkwell_loop, an
    # inkcell_backend - so anything compiling against inkstand/ needs both include paths.
    target_link_libraries(inkstand
        PUBLIC inkstand_core inkcell::inkcell
        PRIVATE inkstand_options
    )
    add_library(inkstand::inkstand ALIAS inkstand)
    message(STATUS "inkstand: nav, form and app over inkcell")
else()
    message(STATUS "inkstand: headless - state and persist only, no inkcell")
endif()

if(INKSTAND_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
