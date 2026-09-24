/*
 * The half that draws, and its link line: inkstand::inkstand resolves inkcell over the same
 * inkwell the headless half uses.
 *
 * inkcell carries an inkwell submodule of its own. It only builds that copy when no target of
 * the name exists yet, and this tree adds its own first, so one inkwell is compiled and both
 * libraries link it. Two would be a configure error rather than a subtle one, which is why this
 * case does not try to detect it - it only has to link.
 *
 * Replaced, like tree_core.c, the moment nav/, form/ or app/ has a case of its own to run.
 */
#include "framework/inkstand_test.h"

#include "inkcell/ui/key.h"

INKSTAND_TEST_CASE(ui_links_inkcell, unit) {
    INKSTAND_TEST_FAIL_IF(inkcell_key_from_name("a") != INKCELL_KEY_A,
                          "inkcell should name the A key");
    record_success(test_name);
}
