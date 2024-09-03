bits: bits.cpp input_bit_stream.h output_bit_stream.h
	$(CXX) -o $@ -O2 -fsanitize=undefined -Wall -Wextra -pedantic -Werror $^
