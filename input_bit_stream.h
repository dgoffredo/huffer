#include <cstddef>
#include <cstdint>
#include <streambuf>

class InputBitStream {
  // `streambuf` is a source of bytes from which bits are read.
  std::streambuf& streambuf;
  // `current` is the "buffer" of the byte most recently read from `streambuf`.
  std::byte current;
  // `mask` is a bit mask that identifies the next bit in `current` to be
  // returned by `get()`. If `mask` is zero, then it is time to read another
  // byte into `current` from `streambuf`.
  std::byte mask;
  // These are the status bits. See the corresponding accessor functions for
  // their meaning.
  bool eof_bit : 1;
  bool fail_bit : 1;
  bool bad_bit : 1;

public:
  explicit InputBitStream(std::streambuf& streambuf);

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

  // Read a bit from the input. Return the value of the bit on success (`0` or
  // `1`), or return `-1` if a bit could not be read.
  // If a bit could not be read, `fail()` will subsequently return `true`.
  // If a bit could not be read due to the end of the input being reached,
  // `eof()` will subsequently return `true`.
  // If a bit could not be read due to an error, `bad()` will subsequently
  // return `true`.
  int get();
};

InputBitStream& operator>>(InputBitStream&, bool&);

inline
InputBitStream::InputBitStream(std::streambuf& streambuf)
: streambuf(streambuf)
, current{0}
, mask{0}
, eof_bit(false)
, fail_bit(false)
, bad_bit(false) {
}

inline
int InputBitStream::get() {
  if (mask == std::byte{0}) {
    try {
      const int ch = streambuf.sbumpc();
      if (ch == std::streambuf::traits_type::eof()) {
        eof(true);
        fail(true);
        return -1;
      }
      current = std::byte{std::uint8_t(ch & 0xff)};
      mask = std::byte{1};
    } catch (...) {
      bad(true);
      return -1;
    }
  }

  const int bit = (current & mask) != std::byte{0};
  mask <<= 1;
  return bit;
}

inline
InputBitStream& operator>>(InputBitStream& stream, bool& bit) {
  const int result = stream.get();
  if (result != -1) {
    bit = bool(result);
  }
  return stream;
}
