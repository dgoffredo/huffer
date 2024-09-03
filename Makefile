PHONY: all
all: bits huffer

huffer: huffer.cpp
	$(CXX) -o $@ --std=c++20 -Og -fsanitize=undefined -Wall -Wextra -pedantic -Werror $^

bits: bits.cpp input_bit_stream.h output_bit_stream.h
	$(CXX) -o $@ --std=c++20 -flto -O3 -Wall -Wextra -pedantic -Werror $^
