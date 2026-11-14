# Thin wrappers over cmake/ctest. The build types live in CMakeLists.txt; nothing here
# configures anything the plain cmake commands would not.

BUILD ?= build

.PHONY: all venv test debug headless format format-check clean

all: debug

# The host tools in requirements-dev.txt - the Python inkwell's Mbed TLS generates its sources
# with, and the pinned clang-format - in a venv of their own, remade when that file changes.
VENV = $(BUILD)/venv
VENV_STAMP = $(VENV)/.installed
CMAKE_PYTHON = -DPython3_EXECUTABLE=$(abspath $(VENV))/bin/python3

venv: $(VENV_STAMP)
$(VENV_STAMP): requirements-dev.txt
	python3 -m venv $(VENV)
	$(VENV)/bin/pip install --quiet --disable-pip-version-check -r requirements-dev.txt
	@touch $@

debug: $(VENV_STAMP)
	cmake -S . -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Debug $(CMAKE_PYTHON)
	cmake --build $(BUILD)

# The verify step, and what CI runs: the unit suite plus the check that no layer reaches the
# wrong way. Run it before every push.
test: debug
	ctest --test-dir $(BUILD) --output-on-failure

# The headless half alone, in a tree of its own: state/ and persist/ over inkwell, with no
# inkcell configured at all. If something in those two areas has started to need the toolkit,
# this is where it stops linking.
headless: $(VENV_STAMP)
	cmake -S . -B $(BUILD)/headless -G Ninja -DCMAKE_BUILD_TYPE=Debug -DINKSTAND_WITH_UI=OFF \
	  $(CMAKE_PYTHON)
	cmake --build $(BUILD)/headless
	ctest --test-dir $(BUILD)/headless --output-on-failure

# What clang-format is allowed to touch, named once. third_party/ is the two submodules, which
# git ls-files does not descend into anyway; the exclusion is there for the day something is
# vendored here, for inkwell's reason.
FORMAT_FILES = $$(git ls-files --cached --others --exclude-standard '*.c' '*.h' ':!:third_party/*')
CLANG_FORMAT ?= $(VENV)/bin/clang-format

format: $(VENV_STAMP)
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

# What CI runs: the same files, changing nothing, failing on the first that differs.
format-check: $(VENV_STAMP)
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

clean:
	rm -rf $(BUILD)
