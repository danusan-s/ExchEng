# ===== Configuration =====
BUILD_DIR := build
CMAKE := cmake
CTEST := ctest

BUILD_TYPE ?= Release

# ===== Default =====
.PHONY: all
all: build

# ===== Clean =====
.PHONY: clean
clean:
	@rm -rf $(BUILD_DIR)


# ===== Build (normal) =====
.PHONY: build
build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	@$(CMAKE) --build $(BUILD_DIR) -j

# ===== Build (Debug release) =====
.PHONY: debug
debug:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo
	@$(CMAKE) --build $(BUILD_DIR) -j

# ===== Rebuild (clean + build) =====
.PHONY: rebuild
rebuild: clean build
