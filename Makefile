CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
TESTS = economics_test block_format_test block_validation_test chain_index_test difficulty_test block_store_test active_chain_test local_node_test

.PHONY: all node test sanitize clean

all: node

node: build/onuros-local-node

test: $(addprefix build/,$(TESTS))
	@for test in $(TESTS); do ./build/$$test || exit $$?; done

build/onuros-local-node: apps/local_node_main.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Icore/include $< -o $@

build/%_test: tests/%_test.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Icore/include $< -o $@

sanitize:
	mkdir -p build
	@for source in tests/*_test.cpp; do \
	  output=build/$$(basename $$source .cpp)_sanitized; \
	  $(CXX) -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror \
	    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	    -Icore/include $$source -o $$output || exit $$?; \
	  $$output || exit $$?; \
	done

clean:
	$(RM) $(addprefix build/,$(TESTS)) build/onuros-local-node
