# One binary built from many translation units: a framework and one file per subject area under
# suites/.
#
# Cases register themselves from constructors (see framework/inkstand_test.h), which is why the
# suite files are listed here directly instead of being collected into a static library first: a
# linker is free to drop library members nothing references, and it would take every test inside
# them with it, silently. Add a new suite file to the list for its half and it runs.

# The headless half: state/ and persist/. These build and run with INKSTAND_WITH_UI=OFF, which
# is the proof they need nothing from inkcell.
set(INKSTAND_TEST_CORE_SUITES
    suites/tree_core.c
)

# The half that draws: nav/, form/, app/.
set(INKSTAND_TEST_UI_SUITES
    suites/nav_frame_scheduler.c
)

set(INKSTAND_TEST_SUITES ${INKSTAND_TEST_CORE_SUITES})
if(INKSTAND_WITH_UI)
    list(APPEND INKSTAND_TEST_SUITES ${INKSTAND_TEST_UI_SUITES})
endif()

add_executable(inkstand_tests
    framework/inkstand_test.c
    ${INKSTAND_TEST_SUITES}
)

target_include_directories(inkstand_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(INKSTAND_WITH_UI)
    target_link_libraries(inkstand_tests PRIVATE inkstand::inkstand inkstand_options)
else()
    target_link_libraries(inkstand_tests PRIVATE inkstand::core inkstand_options)
endif()

add_test(NAME inkstand_unit COMMAND inkstand_tests)
set_tests_properties(inkstand_unit PROPERTIES LABELS unit)

# The layering rule the compiler cannot see, and the script's own cases - a check that has
# only ever been run over a tree with nothing in it has never been seen to fail. See
# scripts/check-layers.py.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME inkstand_layers
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check-layers.py)
    set_tests_properties(inkstand_layers PROPERTIES LABELS lint)

    add_test(NAME inkstand_layers_self_test
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/check-layers.py
                     --self-test)
    set_tests_properties(inkstand_layers_self_test PROPERTIES LABELS lint)
endif()
