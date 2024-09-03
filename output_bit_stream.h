#include <bitset>
#include <cstddef>
#include <cstdint>
#include <streambuf>
#include <vector>

class OutputBitStream {
  // TODO
  std::streambuf& streambuf;
  // TODO
  std::byte current;
  // TODO
  std::byte mask;
  // TODO
  bool bad_bit : 1;

public:
  explicit OutputBitStream(std::streambuf& sink);

  // Send any remaining bits to the sink, but don't flush the sink.
  ~OutputBitStream();

  // TODO
  bool bad() const { return bad_bit; }
  bool fail() const { return bad_bit; }

  explicit operator bool() const { return !fail(); }

  // TODO
  void bad(bool bit) { bad_bit = bit; }

  // Buffer the specified `bit` for writing to the output. Return a reference
  // to this object.
  OutputBitStream& put(bool bit);

  // Write any buffered output bits to the sink (i.e. the destination
  // `std::streambuf`), but don't flush the sink. If there is less than a byte
  // buffered, then pad the extra high order bits with zeros before writing to
  // the sink.
  OutputBitStream& flush_byte();
};

OutputBitStream& operator<<(OutputBitStream&, bool);
OutputBitStream& operator<<(OutputBitStream&, char);
OutputBitStream& operator<<(OutputBitStream&, const std::vector<bool>&);
template <std::size_t n>
OutputBitStream& operator<<(OutputBitStream&, const std::bitset<n>&);

inline
OutputBitStream::OutputBitStream(std::streambuf& sink)
: streambuf(sink)
, current{0}
, mask{1}
, bad_bit{false} {
}

inline
OutputBitStream::~OutputBitStream() {
  flush_byte();
}

inline
OutputBitStream& OutputBitStream::put(bool bit) {
  if (bad()) {
    return *this;
  }

  if (mask == std::byte{0}) {
    if (!flush_byte()) {
      return *this;
    }
  }

  if (bit) {
    current |= mask;
  }
  mask <<= 1;
  return *this;
}

inline
OutputBitStream& OutputBitStream::flush_byte() {
  if (bad()) {
    return *this;
  }

  if (mask == std::byte{1}) {
    return *this;
  }

  try {
    const int result = streambuf.sputc(char(std::uint8_t(current)));
    if (result == std::streambuf::traits_type::eof()) {
      bad(true);
      return *this;
    }
  } catch (...) {
    bad(true);
    return *this;
  }

  current = std::byte{0};
  mask = std::byte{1};
  return *this;
}

inline
OutputBitStream& operator<<(OutputBitStream& stream, bool bit) {
  return stream.put(bit);
}

inline
OutputBitStream& operator<<(OutputBitStream& stream, char ch) {
  const std::uint8_t byte = ch;
  for (int i = 0; i < 8 && stream; ++i) {
    stream << bool(byte & (1 << i));
  }
  return stream;
}

inline
OutputBitStream& operator<<(OutputBitStream& stream, const std::vector<bool>& bits) {
  for (int i = 0; i < int(bits.size()) && stream; ++i) {
    stream << bits[i];
  }
  return stream;
}

template <std::size_t n>
OutputBitStream& operator<<(OutputBitStream& stream, const std::bitset<n>& bits) {
  for (int i = 0; i < int(bits.size()) && stream; ++i) {
    stream << bits[i];
  }
  return stream;
}
