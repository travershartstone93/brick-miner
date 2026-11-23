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
