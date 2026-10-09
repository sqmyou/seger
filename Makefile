# Seger chess engine
#
# Targets:
#   make            build the engine (build/seger)
#   make test       build and run the unit + perft tests
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

TEST_BINS := $(BUILD_DIR)/perft_test $(BUILD_DIR)/position_test

.PHONY: all test divide clean

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

# Perft-divide helper: ./build/perft_divide "<fen>" <depth>
$(BUILD_DIR)/perft_divide: $(TEST_DIR)/perft_divide.cpp $(TEST_COMMON_OBJS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $< $(TEST_COMMON_OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

test: $(TEST_BINS)
	@echo "== position_test =="
	@$(BUILD_DIR)/position_test
	@echo "== perft_test =="
	@$(BUILD_DIR)/perft_test

divide: $(BUILD_DIR)/perft_divide

clean:
	rm -rf $(BUILD_DIR)
