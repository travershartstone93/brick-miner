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

$(BUILD_DIR)/rsi-score: rsi/score.cpp $(BUILD_DIR)/host/common.o
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $^ $(HOST_LIBS) -o "$@"

.PHONY: check-project
check-project: $(BUILD_DIR)/project-miner $(BUILD_DIR)/brick-scan $(BUILD_DIR)/project-regression
	"$(BUILD_DIR)/project-regression"

$(BUILD_DIR)/package-pilot: project-input/package.cpp $(addprefix $(BUILD_DIR)/host/,$(addsuffix .o,common probes project link))
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $^ $(HOST_LIBS) -o "$@"

V5_OBJECTS = acquisition integration projections database worker queue latch fifo measures xxhash sequences prefix cli
$(BUILD_DIR)/v5/%.o: v5/native/%.cpp v5/native/pilot.hpp
	mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) -MMD -MP -c "$(abspath $<)" -o "$@"
$(BUILD_DIR)/v5-miner: $(addprefix $(BUILD_DIR)/v5/,$(addsuffix .o,$(V5_OBJECTS))) $(addprefix $(BUILD_DIR)/host/,$(addsuffix .o,$(HOST_OBJECTS))) | $(BUILD_DIR)/brick-mine $(BUILD_DIR)/brick-scan
	$(CXX) $^ $(HOST_LIBS) -lsqlite3 -o "$@"
-include $(wildcard $(BUILD_DIR)/v5/*.d)

$(BUILD_DIR)/replay-gaps: replay-gaps/replay.cpp $(BUILD_DIR)/host/common.o $(BUILD_DIR)/host/bundle.o
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $^ $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/telemetry-replay: replay-gaps/telemetry.cpp $(BUILD_DIR)/host/common.o $(BUILD_DIR)/host/bundle.o
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $^ $(HOST_LIBS) -o "$@"

ROW_REPLAY_INPUTS = replay-gaps/row-trial.cpp replay-gaps/row-trial.hpp $(BUILD_DIR)/host/common.o $(BUILD_DIR)/host/bundle.o
$(BUILD_DIR)/path-replay: replay-gaps/path-metadata.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/row-boundary-replay: replay-gaps/row-boundaries.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/reachability-replay: replay-gaps/reachability.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/sensor-history-replay: replay-gaps/sensor-history.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/booking-state-replay: replay-gaps/booking-state.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/row-order-replay: replay-gaps/row-order.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/stock-state-replay: replay-gaps/stock-state.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/sensor-conflicts-replay: replay-gaps/sensor-conflicts.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/xml-chain-replay: replay-gaps/xml-chains.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/zip-chain-replay: replay-gaps/zip-chains.cpp replay-gaps/zip-fixture.hpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -larchive -o "$@"

$(BUILD_DIR)/directory-count-replay: replay-gaps/directory-count.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/depot-count-replay: replay-gaps/depot-count.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/typed-reconciliation: replay-gaps/typed-reconciliation.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/typed-uri-replay: replay-gaps/typed-uri.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/typed-closure-replay: replay-gaps/typed-closure.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/coverage-audit: replay-gaps/coverage-audit.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/residual-closure-replay: replay-gaps/residual-closure.cpp replay-gaps/xml-fixture.hpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(shell pkg-config --cflags libxml-2.0) $(filter %.cpp %.o,$^) $(HOST_LIBS) $(shell pkg-config --libs libxml-2.0) -o "$@"

$(BUILD_DIR)/residual-uri-replay: replay-gaps/residual-uri.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/residual-zip-replay: replay-gaps/residual-zip.cpp replay-gaps/zip-fixture.hpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -larchive -o "$@"

$(BUILD_DIR)/residual-directory-replay: replay-gaps/residual-directory.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/residual-ranking: replay-gaps/residual-ranking.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/unordered-singletons-replay: replay-gaps/unordered-singletons.cpp replay-gaps/xml-fixture.hpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(shell pkg-config --cflags libxml-2.0) $(filter %.cpp %.o,$^) $(HOST_LIBS) $(shell pkg-config --libs libxml-2.0) -o "$@"

$(BUILD_DIR)/unordered-telemetry-replay: replay-gaps/unordered-telemetry.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/unordered-scheduling-replay: replay-gaps/unordered-scheduling.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/unordered-dispatch-replay: replay-gaps/unordered-dispatch.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/unordered-bookings-replay: replay-gaps/unordered-bookings.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/unordered-finance-replay: replay-gaps/unordered-finance.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/uri-fragment-replay: replay-gaps/uri-fragments.cpp $(ROW_REPLAY_INPUTS)
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -o "$@"

$(BUILD_DIR)/library-pilot: src/experiments/discovery.cpp src/experiments/pilot.cpp src/experiments/pilots.hpp src/experiments/original_adapter.hpp $(BUILD_DIR)/host/common.o $(BUILD_DIR)/host/bundle.o
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -lpugixml -o "$@"

$(BUILD_DIR)/shared-review: shared/requests.cpp shared/consumer.cpp shared/review.cpp shared/qualification.hpp $(BUILD_DIR)/host/common.o $(BUILD_DIR)/host/bundle.o
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -larchive -lsqlite3 -o "$@"

$(BUILD_DIR)/experiments/%.o: src/experiments/%.cpp
	mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -Isrc/host -fPIC -std=c++20 -Wall -Wextra $(HOST_FLAGS) -MMD -MP -c "$(abspath $<)" -o "$@"
-include $(wildcard $(BUILD_DIR)/experiments/*.d)

$(BUILD_DIR)/extract-row-kernels: row-ops/mined/source/extract.cpp $(BUILD_DIR)/experiments/duckdb.o $(BUILD_DIR)/host/common.o
	$(CXX) $(CXXFLAGS) -std=c++20 -fPIC -Isrc/host -Isrc/experiments $(HOST_FLAGS) $^ $(HOST_LIBS) -o "$@"

QUALIFICATION_OBJECTS = $(addprefix $(BUILD_DIR)/experiments/,$(addsuffix .o,discovery duckdb qualification)) $(BUILD_DIR)/host/common.o $(BUILD_DIR)/host/bundle.o
$(BUILD_DIR)/row-pilot: row-ops/driver.cpp row-ops/qualification.cpp shared/requests.cpp $(QUALIFICATION_OBJECTS)
	$(CXX) $(CXXFLAGS) -std=c++20 -fPIC -Isrc/host -Isrc/experiments $(HOST_FLAGS) $^ $(HOST_LIBS) -lpugixml -o "$@"

$(BUILD_DIR)/shared/%.o: shared/%.cpp
	mkdir -p "$(dir $@)"
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host -Isrc/experiments $(HOST_FLAGS) -MMD -MP -c "$(abspath $<)" -o "$@"
-include $(wildcard $(BUILD_DIR)/shared/*.d)
$(BUILD_DIR)/shared-pilot: $(addprefix $(BUILD_DIR)/shared/,$(addsuffix .o,start build qualify requests consumer)) $(QUALIFICATION_OBJECTS)
	$(CXX) $^ $(HOST_LIBS) -larchive -lsqlite3 -lpugixml -o "$@"

$(BUILD_DIR)/shared-ledger: $(addprefix $(BUILD_DIR)/shared/,$(addsuffix .o,ledger requests)) $(BUILD_DIR)/experiments/discovery.o $(BUILD_DIR)/host/bundle.o $(BUILD_DIR)/host/common.o
	$(CXX) $^ $(HOST_LIBS) -lpugixml -o "$@"

$(BUILD_DIR)/language-check: tests/language.cpp
	mkdir -p "$(BUILD_DIR)"
	$(CXX) $(CXXFLAGS) -std=c++20 -Wall -Wextra $< -o "$@"

.PHONY: check-language check
check-language: $(BUILD_DIR)/language-check
	"$<" .
check: check-language check-project

$(BUILD_DIR)/v5-recovery: tests/v5/recovery.cpp $(addprefix $(BUILD_DIR)/v5/,$(addsuffix .o,$(filter-out cli,$(V5_OBJECTS)))) $(addprefix $(BUILD_DIR)/host/,$(addsuffix .o,$(HOST_OBJECTS)))
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host -Iv5/native $(HOST_FLAGS) $^ $(HOST_LIBS) -lsqlite3 -o "$@"

.PHONY: check-v5-queue
check-v5-queue: $(BUILD_DIR)/v5-miner $(BUILD_DIR)/v5-recovery $(BUILD_DIR)/brick-scan
	"$(BUILD_DIR)/v5-recovery"

V5_INVENTORY_OBJECTS = inventory_main replay_usage binding_identity writer uri_reader recursive_files json_writer archive_names file_records parent_paths filter_extension file_qualification relational_rows
$(BUILD_DIR)/v5-inventory: $(addprefix $(BUILD_DIR)/v5/,$(addsuffix .o,$(V5_INVENTORY_OBJECTS) $(filter-out cli,$(V5_OBJECTS)))) $(addprefix $(BUILD_DIR)/host/,$(addsuffix .o,$(HOST_OBJECTS))) | $(BUILD_DIR)/replay-gaps
	$(CXX) $^ $(HOST_LIBS) -lsqlite3 -o "$@"

$(BUILD_DIR)/v5-gap-review: v5/native/gap_review.cpp $(addprefix $(BUILD_DIR)/v5/,$(addsuffix .o,$(filter-out cli,$(V5_OBJECTS)))) $(addprefix $(BUILD_DIR)/host/,$(addsuffix .o,$(HOST_OBJECTS)))
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host $(HOST_FLAGS) $^ $(HOST_LIBS) -lsqlite3 -o "$@"

$(BUILD_DIR)/v5-gap-check: tests/v5/gap.cpp $(addprefix $(BUILD_DIR)/v5/,$(addsuffix .o,$(filter-out cli,$(V5_OBJECTS)))) $(addprefix $(BUILD_DIR)/host/,$(addsuffix .o,$(HOST_OBJECTS)))
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host -Iv5/native $(HOST_FLAGS) $^ $(HOST_LIBS) -lsqlite3 -o "$@"

.PHONY: check-v5-gap
check-v5-gap: $(BUILD_DIR)/v5-gap-review $(BUILD_DIR)/v5-gap-check
	"$(BUILD_DIR)/v5-gap-check"

$(BUILD_DIR)/v5-xml-domain: tests/v5/xml-domain.cpp tests/v5/xml-restricted.hpp src/experiments/original_adapter.hpp $(addprefix $(BUILD_DIR)/v5/,$(addsuffix .o,$(filter-out cli,$(V5_OBJECTS)))) $(addprefix $(BUILD_DIR)/host/,$(addsuffix .o,$(HOST_OBJECTS))) | $(BUILD_DIR)/brick-mine
	$(CXX) $(CXXFLAGS) -fPIC -std=c++20 -Isrc/host -Iv5/native $(HOST_FLAGS) $(filter %.cpp %.o,$^) $(HOST_LIBS) -lsqlite3 -o "$@"

.PHONY: check-v5-xml-domain
check-v5-xml-domain: $(BUILD_DIR)/v5-xml-domain
	"$(BUILD_DIR)/v5-xml-domain"
