.PHONY: all
all: bits huffer

huffer: huffer.cpp
	$(CXX) -o $@ --std=c++20 -O2 -Wall -Wextra -pedantic -Werror $^

bits: bits.cpp input_bit_stream.h output_bit_stream.h
	$(CXX) -o $@ --std=c++20 -flto -O2 -Wall -Wextra -pedantic -Werror $^
