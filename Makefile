CXX = clang++
CXXFLAGS ?= -O1 -g0
BUILD_DIR ?= .build

$(BUILD_DIR)/brick-mine: src/miner.cpp
	mkdir -p "$(BUILD_DIR)"
	$(CXX) $(CXXFLAGS) -DMINER_RESOURCE_DIR='"$(shell clang++ -print-resource-dir)"' -std=c++17 -Wall -Wextra -Wno-unused-parameter $< -o $@ -lclang-cpp -lLLVM

$(BUILD_DIR)/brick-scan: src/project_scan.cpp
	mkdir -p "$(BUILD_DIR)"
	@set -e; binary=$$(mktemp "$@.XXXXXX"); trap 'rm -f "$$binary"' EXIT; \
	$(CXX) $(CXXFLAGS) -DMINER_RESOURCE_DIR='"$(shell clang++ -print-resource-dir)"' -std=c++17 -Wall -Wextra -Wno-unused-parameter "$<" -o "$$binary" -lclang-cpp -lLLVM; \
	chmod +x "$$binary"; mv -f "$$binary" "$@"

HOST_FLAGS := $(shell pkg-config --cflags Qt6Core jsoncpp)
HOST_LIBS := $(shell pkg-config --libs Qt6Core jsoncpp)
$(BUILD_DIR)/host/%.o: src/host/%.cpp src/host/common.hpp
	mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Wall -Wextra $(HOST_FLAGS) -MMD -MP -c "$(abspath $<)" -o "$@"

-include $(wildcard $(BUILD_DIR)/host/*.d)

HOST_OBJECTS = common link probes project bundle publication
$(BUILD_DIR)/project-miner: $(addprefix $(BUILD_DIR)/host/,$(addsuffix .o,$(HOST_OBJECTS) cli))
	$(CXX) $^ $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/project-regression: tests/project/regression.cpp $(BUILD_DIR)/host/common.o
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $^ $(HOST_LIBS) -o "$@"

.PHONY: check-project
check-project: $(BUILD_DIR)/project-miner $(BUILD_DIR)/brick-scan $(BUILD_DIR)/project-regression
	"$(BUILD_DIR)/project-regression"

$(BUILD_DIR)/package-pilot: project-input/package.cpp $(addprefix $(BUILD_DIR)/host/,$(addsuffix .o,common probes project link))
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $^ $(HOST_LIBS) -o "$@"

V5_OBJECTS = acquisition integration projections database cli
$(BUILD_DIR)/v5/%.o: v5/native/%.cpp v5/native/pilot.hpp
	mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) -MMD -MP -c "$(abspath $<)" -o "$@"
$(BUILD_DIR)/v5-miner: $(addprefix $(BUILD_DIR)/v5/,$(addsuffix .o,$(V5_OBJECTS))) $(addprefix $(BUILD_DIR)/host/,$(addsuffix .o,$(HOST_OBJECTS)))
	$(CXX) $^ $(HOST_LIBS) -lsqlite3 -o "$@"
-include $(wildcard $(BUILD_DIR)/v5/*.d)

$(BUILD_DIR)/replay-gaps: replay-gaps/replay.cpp $(BUILD_DIR)/host/common.o $(BUILD_DIR)/host/bundle.o
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $^ $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/library-pilot: src/experiments/discovery.cpp src/experiments/pilot.cpp src/experiments/pilots.hpp $(BUILD_DIR)/host/common.o $(BUILD_DIR)/host/bundle.o
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -lpugixml -o "$@"

$(BUILD_DIR)/shared-review: shared/requests.cpp shared/consumer.cpp shared/review.cpp shared/qualification.hpp $(BUILD_DIR)/host/common.o $(BUILD_DIR)/host/bundle.o
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -larchive -lsqlite3 -o "$@"
