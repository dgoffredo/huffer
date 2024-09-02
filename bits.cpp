#include "input_bit_stream.h"
#include <bitset>
#include <iostream>

int main() {
  const int bytes_per_line = 8;
  InputBitStream stream{*std::cin.rdbuf()};
  for (int i = 0;; ++i) {
    std::bitset<8> byte;
    for (int j = 0; j < 8; ++j) {
      bool bit;
      stream >> bit;
      if (!stream) {
        std::cout << '\n';
        return j != 0;
      }
      byte[j] = bit;
    }
    const char sep =  (i % bytes_per_line == (bytes_per_line - 1) ? '\n' : ' ');
    std::cout << byte << sep;
  }
  std::cout << '\n';
}
