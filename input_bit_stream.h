#include <bitset>
#include <cstddef>
#include <cstdint>
#include <streambuf>

class InputBitStream {
  // `streambuf` is a source of bytes from which bits are read.
  std::streambuf& streambuf;
  // `current` is the "buffer" of the byte most recently read from `streambuf`.
  std::byte current;
  // `mask` is a bit mask that identifies the next bit in `current` to be
  // returned by `get`. If `mask` is zero, then it is time to read another byte
  // into `current` from `streambuf`.
  std::byte mask;
  // These are the status bits. See the corresponding accessor functions for
  // their meaning.
  bool eof_bit : 1;
  bool fail_bit : 1;
  bool bad_bit : 1;

public:
  explicit InputBitStream(std::streambuf& source);

  // If `eof()`, then the stream has exhausted the input bits.
  bool eof() const { return eof_bit; }
  // If `fail()`, then the previous input operation did not consume all of the
  // data requested, either due to a lack of input (`eof()`) or due to an error
  // (`bad()`).
  bool fail() const { return fail_bit; }
  // If `bad()`, then an error occurred during a previous input operation,
  // where reaching the end of the input is not considered an error.
  bool bad() const { return bad_bit; }

  explicit operator bool() const {
    return !fail() && !bad();
  }

  // Set the relevant status bit.
  void eof(bool bit) { eof_bit = bit; }
  void fail(bool bit) { fail_bit = bit; }
  void bad(bool bit) { bad_bit = bit; }

  // Assign to the specified `bit` a bit read from the input. Return a
  // reference to this object.
  // If a bit could not be read, `fail()` will subsequently return `true`.
  // If a bit could not be read due to the end of the input being reached,
  // `eof()` will subsequently return `true`.
  // If a bit could not be read due to an error, `bad()` will subsequently
  // return `true`.
  InputBitStream& get(bool& bit);
};

InputBitStream& operator>>(InputBitStream&, bool&);
template <std::size_t n>
InputBitStream& operator>>(InputBitStream&, std::bitset<n>&);

inline
InputBitStream::InputBitStream(std::streambuf& source)
: streambuf(source)
, current{0}
, mask{0}
, eof_bit(false)
, fail_bit(false)
, bad_bit(false) {
}

inline
InputBitStream& InputBitStream::get(bool& bit) {
  // If we're out of bits in `current`, read another byte.
  if (mask == std::byte{0}) {
    int ch;
    try {
      ch = streambuf.sbumpc();
    } catch (...) {
      bad(true);
      fail(true);
      return *this;
    }
    if (ch == std::streambuf::traits_type::eof()) {
      eof(true);
      fail(true);
      return *this;
    }
    current = std::byte{std::uint8_t(ch & 0xff)};
    mask = std::byte{1};
  }

  bit = (current & mask) != std::byte{0};
  mask <<= 1;
  return *this;
}

inline
InputBitStream& operator>>(InputBitStream& stream, bool& bit) {
  return stream.get(bit);
}

template <std::size_t n>
InputBitStream& operator>>(InputBitStream& stream, std::bitset<n>& bits) {
  for (int i = 0; i < int(bits.size()); ++i) {
    bool bit;
    stream >> bit;
    if (!stream) {
      break;
    }
    bits[i] = bit;
  }
  return stream;
}
