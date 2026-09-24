# Thin wrappers over cmake/ctest. The build types live in CMakeLists.txt; nothing here
# configures anything the plain cmake commands would not.

BUILD ?= build

.PHONY: all test debug headless format format-check clean

all: debug

debug:
	cmake -S . -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD)

# The verify step, and what CI runs: the unit suite plus the check that no layer reaches the
# wrong way. Run it before every push.
test: debug
	ctest --test-dir $(BUILD) --output-on-failure

# The headless half alone, in a tree of its own: state/ and persist/ over inkwell, with no
# inkcell configured at all. If something in those two areas has started to need the toolkit,
# this is where it stops linking.
headless:
	cmake -S . -B $(BUILD)/headless -G Ninja -DCMAKE_BUILD_TYPE=Debug -DINKSTAND_WITH_UI=OFF
	cmake --build $(BUILD)/headless
	ctest --test-dir $(BUILD)/headless --output-on-failure

# What clang-format is allowed to touch, named once. third_party/ is the two submodules, which
# git ls-files does not descend into anyway; the exclusion is there for the day something is
# vendored here, for inkwell's reason.
FORMAT_FILES = $$(git ls-files --cached --others --exclude-standard '*.c' '*.h' ':!:third_party/*')
CLANG_FORMAT ?= clang-format

format:
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

# What CI runs: the same files, changing nothing, failing on the first that differs.
format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

clean:
	rm -rf $(BUILD)
