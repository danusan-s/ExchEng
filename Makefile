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

# ===== Test =====
.PHONY: test
test:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	@$(CMAKE) --build $(BUILD_DIR) -j --target build-tests
	@cd $(BUILD_DIR) && $(CTEST) --output-on-failure

# ===== Benchmark =====
.PHONY: benchmark
benchmark:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
	@$(CMAKE) --build $(BUILD_DIR) -j --target LoggerBenchmark MemPoolBenchmark
	@./$(BUILD_DIR)/benchmark/logger/LoggerBenchmark
	@./$(BUILD_DIR)/benchmark/mem_pool/MemPoolBenchmark

# ===== Run exchange =====
.PHONY: run_exchange
run_exchange:
	./build/ExchEng

# ===== Run clients =====
.PHONY: run_clients
run_clients:
	./scripts/run_clients.sh

# ===== Run Demo =====
.PHONY: run_demo
run_demo:
	./scripts/run_exchange_and_clients.sh

