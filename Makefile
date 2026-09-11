CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
TESTS = economics_test block_format_test block_validation_test chain_index_test difficulty_test

.PHONY: test sanitize clean

test: $(addprefix build/,$(TESTS))
	@for test in $(TESTS); do ./build/$$test || exit $$?; done

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
	$(RM) $(addprefix build/,$(TESTS))
