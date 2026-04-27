# Convenience wrappers around the CMake build for squelch-tr-uploader.
#
# CMake / Ninja are the source of truth — this Makefile exists so contributors
# can run `make`, `make clean`, `make lint` etc. without remembering the full
# cmake invocation. All targets are .PHONY; nothing is tracked as a real file.

PLUGIN_DIR  := plugin
BUILD_DIR   := $(PLUGIN_DIR)/build
BUILD_TYPE  ?= Debug
GENERATOR   ?= Ninja
JOBS        ?=

# Plugin source files (single-source, mirrors TR's bundled uploaders).
SOURCES     := $(PLUGIN_DIR)/squelch_uploader.cc

# Tool discovery — prefer system tools, fall back to the C/C++ extension's copy.
CLANG_FORMAT ?= $(shell command -v clang-format 2>/dev/null || \
    ls $(HOME)/.vscode-server/extensions/ms-vscode.cpptools-*/LLVM/bin/clang-format 2>/dev/null | head -n1)
CLANG_TIDY   ?= $(shell command -v clang-tidy 2>/dev/null || \
    ls $(HOME)/.vscode-server/extensions/ms-vscode.cpptools-*/LLVM/bin/clang-tidy 2>/dev/null | head -n1)
CPPCHECK     ?= $(shell command -v cppcheck 2>/dev/null)

CMAKE_FLAGS := -S $(PLUGIN_DIR) -B $(BUILD_DIR) -G "$(GENERATOR)" \
               -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

ifdef SQUELCH_TR_TAG
CMAKE_FLAGS += -DSQUELCH_TR_TAG=$(SQUELCH_TR_TAG)
endif

BUILD_FLAGS := --build $(BUILD_DIR)
ifdef JOBS
BUILD_FLAGS += -j $(JOBS)
endif

.PHONY: help all build configure rebuild clean distclean install \
        format format-check tidy cppcheck lint print-vars

help:
	@echo "squelch-tr-uploader — make targets"
	@echo ""
	@echo "  make / make build   Configure (if needed) and build the plugin"
	@echo "  make configure      Run cmake configure step only"
	@echo "  make rebuild        Clean build artifacts and rebuild from scratch"
	@echo "  make clean          Remove compiled objects (keeps cmake cache)"
	@echo "  make distclean      Remove the entire build directory"
	@echo "  make install        Install to CMAKE_INSTALL_PREFIX (default /usr/local)"
	@echo ""
	@echo "  make format         Run clang-format -i over plugin sources"
	@echo "  make format-check   Verify formatting without modifying files"
	@echo "  make tidy           Run clang-tidy against compile_commands.json"
	@echo "  make cppcheck       Run cppcheck static analysis"
	@echo "  make lint           format-check + tidy + cppcheck"
	@echo ""
	@echo "Variables:"
	@echo "  BUILD_TYPE=$(BUILD_TYPE)   (Debug | Release | RelWithDebInfo)"
	@echo "  GENERATOR=$(GENERATOR)"
	@echo "  JOBS=<N>           Parallel build jobs"
	@echo "  SQUELCH_TR_TAG=<tag>  Trunk-Recorder tag/commit to compile against"

all: build

$(BUILD_DIR)/build.ninja $(BUILD_DIR)/Makefile:
	cmake $(CMAKE_FLAGS)

configure:
	cmake $(CMAKE_FLAGS)

build: configure
	cmake $(BUILD_FLAGS)

rebuild: distclean build

clean:
	@if [ -d $(BUILD_DIR) ]; then cmake --build $(BUILD_DIR) --target clean; fi

distclean:
	rm -rf $(BUILD_DIR)

install: build
	cmake --install $(BUILD_DIR)

# ---------------------------------------------------------------------------
# Linters
# ---------------------------------------------------------------------------

format:
	@if [ -z "$(CLANG_FORMAT)" ]; then \
		echo "clang-format not found"; exit 1; \
	fi
	"$(CLANG_FORMAT)" -i $(SOURCES)

format-check:
	@if [ -z "$(CLANG_FORMAT)" ]; then \
		echo "clang-format not found"; exit 1; \
	fi
	"$(CLANG_FORMAT)" --dry-run --Werror $(SOURCES)

tidy: configure
	@if [ -z "$(CLANG_TIDY)" ]; then \
		echo "clang-tidy not found"; exit 1; \
	fi
	"$(CLANG_TIDY)" -p $(BUILD_DIR) $(SOURCES)

cppcheck:
	@if [ -z "$(CPPCHECK)" ]; then \
		echo "cppcheck not found"; exit 1; \
	fi
	"$(CPPCHECK)" --enable=warning,style,performance,portability \
		--std=c++17 --quiet --inline-suppr \
		--suppress=missingIncludeSystem \
		$(SOURCES)

lint: format-check tidy cppcheck

print-vars:
	@echo "PLUGIN_DIR    = $(PLUGIN_DIR)"
	@echo "BUILD_DIR     = $(BUILD_DIR)"
	@echo "BUILD_TYPE    = $(BUILD_TYPE)"
	@echo "GENERATOR     = $(GENERATOR)"
	@echo "SOURCES       = $(SOURCES)"
	@echo "CLANG_FORMAT  = $(CLANG_FORMAT)"
	@echo "CLANG_TIDY    = $(CLANG_TIDY)"
	@echo "CPPCHECK      = $(CPPCHECK)"
