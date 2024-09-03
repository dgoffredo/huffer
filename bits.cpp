#include "input_bit_stream.h"
#include "output_bit_stream.h"

#include <bitset>
#include <iomanip>
#include <iostream>

void putc_escaped(std::ostream& out, char c) {
  switch (c) {
  case '\a': out << "\\a"; return;
  case '\b': out << "\\b"; return;
  case '\f': out << "\\f"; return;
  case '\n': out << "\\n"; return;
  case '\r': out << "\\r"; return;
  case '\t': out << "\\t"; return;
  case '\v': out << "\\v"; return;
  case '\\': out << "\\'"; return;
  case '\'': out << "'"; return;
  case '\"': out << "\""; return;
  }
  if (c >= 0x20 && c <= 0x7E) {
    out.put(c);
    return;
  }
  out << "\\x" << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(c & 0xff);
}

struct Escaper {
  char ch;
  friend std::ostream& operator<<(std::ostream& stream, Escaper escaper) {
    putc_escaped(stream, escaper.ch);
    return stream;
  }
};

Escaper escaped(char ch) {
  return Escaper{ch};
}

int read_bits() {
  const int bytes_per_line = 8;
  InputBitStream stream{*std::cin.rdbuf()};
  // Example output for the input "hello, world!":
  //
  //     01101000(h) 01100101(e) 01101100(l) 01101100(l) 01101111(o) 00101100( ) 00100000(,) 01110111(w)
  //     01101111(o) 01110010(r) 01101100(l) 01100100(d) 00100001(!)
  //
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
    const char ch = char(byte.to_ulong());
    std::cout << byte << '(' << escaped(ch) << ')' << sep;
  }
  std::cout << '\n';
}

int echo_bits() {
  InputBitStream in{*std::cin.rdbuf()};
  OutputBitStream out{*std::cout.rdbuf()};
  bool bit;
  while (in >> bit) {
    out << bit;
  }
  return in.bad() || out.fail();
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <read | echo>\n";
    return 1;
  }

  const std::string command = argv[1];
  if (command == "read") {
    return read_bits();
  }
  if (command == "echo") {
    return echo_bits();
  }

}
