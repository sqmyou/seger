# Seger chess engine
#
# Targets:
#   make            build the engine (build/seger)
#   make test       build and run the unit, perft and UCI tests
#   make clean      remove build artifacts

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDFLAGS  ?=
LDLIBS   ?= -pthread

BUILD_DIR := build
SRC_DIR   := src
TEST_DIR  := tests

ENGINE_SRCS := $(wildcard $(SRC_DIR)/*.cpp)
ENGINE_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(ENGINE_SRCS))

ENGINE := $(BUILD_DIR)/seger

# Tests link the engine sources but supply their own main().
TEST_COMMON_SRCS := $(filter-out $(SRC_DIR)/main.cpp,$(ENGINE_SRCS))
TEST_COMMON_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(TEST_COMMON_SRCS))

TEST_BINS := $(BUILD_DIR)/perft_test $(BUILD_DIR)/position_test $(BUILD_DIR)/search_test $(BUILD_DIR)/uci_test $(BUILD_DIR)/eval_test

.PHONY: all test divide bench clean

all: $(ENGINE)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -c $< -o $@

$(ENGINE): $(ENGINE_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/perft_test: $(TEST_DIR)/perft_test.cpp $(TEST_COMMON_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $< $(TEST_COMMON_OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/position_test: $(TEST_DIR)/position_test.cpp $(TEST_COMMON_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $< $(TEST_COMMON_OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/search_test: $(TEST_DIR)/search_test.cpp $(TEST_COMMON_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $< $(TEST_COMMON_OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/uci_test: $(TEST_DIR)/uci_test.cpp $(TEST_COMMON_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $< $(TEST_COMMON_OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/eval_test: $(TEST_DIR)/eval_test.cpp $(TEST_COMMON_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $< $(TEST_COMMON_OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

# Perft-divide helper: ./build/perft_divide "<fen>" <depth>
$(BUILD_DIR)/perft_divide: $(TEST_DIR)/perft_divide.cpp $(TEST_COMMON_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $< $(TEST_COMMON_OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

# Also build the engine so bench/self-play never run a stale binary.
test: $(ENGINE) $(TEST_BINS)
	@echo "== position_test =="
	@$(BUILD_DIR)/position_test
	@echo "== perft_test =="
	@$(BUILD_DIR)/perft_test
	@echo "== search_test =="
	@$(BUILD_DIR)/search_test
	@echo "== uci_test =="
	@$(BUILD_DIR)/uci_test
	@echo "== eval_test =="
	@$(BUILD_DIR)/eval_test

divide: $(BUILD_DIR)/perft_divide

# Fixed-depth benchmark: prints a stable node-count signature over eight
# positions at a fixed depth. Use it to compare changes.
bench: $(ENGINE)
	@$(ENGINE) bench 9

clean:
	rm -rf $(BUILD_DIR)
