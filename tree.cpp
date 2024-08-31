#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <istream>
#include <map>
#include <ostream>
#include <queue>
#include <string>
#include <type_traits>
#include <vector>

#ifdef SYMBOL_SIZE
constexpr std::size_t symbol_size = SYMBOL_SIZE;
#else
constexpr std::size_t symbol_size = 1;
#endif

static_assert(symbol_size <= 8);

// A symbol might be a character, or a sequence of two characters, or ...
using Symbol = std::array<char, symbol_size>;

// An internal node ID is a counter large enough to cover all non-leaf nodes in
// a binary tree that has as its leaves all possible values of `Symbol` (at
// worst).
using InternalNodeID =
  std::conditional_t<symbol_size == 1, std::uint8_t,
  std::conditional_t<symbol_size == 2, std::uint16_t,
  std::conditional_t<symbol_size <= 4, std::uint32_t,
  std::uint64_t>>>;

// `SymbolCounts` maps each `Symbol` to how often it appears in the input.
// In case `symbol_size` does not divide the input size, `extra` might contain
// the trailing remainder of the input.
struct SymbolCounts {
  std::map<Symbol, std::uint64_t> counts;
  std::string extra;
};

void putc_dubscaped(std::ostream& out, char c) {
  switch (c) {
  case '\a': out << "\\\\a"; return;
  case '\b': out << "\\\\b"; return;
  case '\f': out << "\\\\f"; return;
  case '\n': out << "\\\\n"; return;
  case '\r': out << "\\\\r"; return;
  case '\t': out << "\\\\t"; return;
  case '\v': out << "\\\\v"; return;
  case '\\': out << "\\\\'"; return;
  case '\'': out << "'"; return;
  case '\"': out << "\\\""; return;
  case '\?': out << "?"; return;
  }
  if (c >= 0x20 && c <= 0x7E) {
    out.put(c);
    return;
  }
  out << "\\\\x" << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(c & 0xff);
}

template <typename Chars>
struct DubscapedPrinter {
  const Chars *chars;
  friend std::ostream& operator<<(std::ostream& out, const DubscapedPrinter& printer) {
    for (const char ch : *printer.chars) {
      putc_dubscaped(out, ch);
    }
    return out;
  }
};

template <typename Chars>
auto dubscaped(const Chars& chars) {
  return DubscapedPrinter<Chars>{.chars = &chars};
}

template <typename Chars>
struct HexedPrinter {
  const Chars *chars;
  friend std::ostream& operator<<(std::ostream& out, const HexedPrinter& printer) {
    for (const char ch : *printer.chars) {
      out << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(ch & 0xff);
    }
    return out;
  }
};

template <typename Chars>
auto hexed(const Chars& chars) {
  return HexedPrinter<Chars>{.chars = &chars};
}

struct Node {
  std::uint64_t weight : 63;
  enum class Type : bool {
    leaf,
    internal
  } which : 1;
  union {
    Symbol leaf;
    InternalNodeID internal;
  };

  void print_name(std::ostream& out) const {
    if (which == Type::leaf) {
      out << "leaf_" << hexed(leaf);
    } else {
      out << "internal_" << std::dec << static_cast<std::uint64_t>(internal);
    }
  }

  void print_label_quoted(std::ostream& out) const {
    if (which == Type::leaf) {
      out << "\"\\\"" << dubscaped(leaf) << "\\\" (" << std::dec << weight << ")\"";
    } else {
      out << "\"(" << std::dec << weight << ")\"";
    }
  }

  struct NamePrinter {
    const Node *node;
    friend std::ostream& operator<<(std::ostream& out, NamePrinter printer) {
      printer.node->print_name(out);
      return out;
    }
  };

  struct LabelQuotedPrinter {
    const Node *node;
    friend std::ostream& operator<<(std::ostream& out, LabelQuotedPrinter printer) {
      printer.node->print_label_quoted(out);
      return out;
    }
  };

  NamePrinter name() const {
    return NamePrinter{.node = this};
  }

  LabelQuotedPrinter label_quoted() const {
    return LabelQuotedPrinter{.node = this};
  }
};

std::istream& operator>>(std::istream& in, SymbolCounts& symbols) {
  Symbol buffer;
  for (;;) {
    in.read(buffer.data(), buffer.size());
    switch (const int count = in.gcount()) {
    case symbol_size:
      ++symbols.counts[buffer];
      continue;
    default:
      symbols.extra.assign(buffer.data(), count);
    case 0:
      return in;
    }
  }
  return in;
}

// We want our heap (`priority_queue`) to be a min-heap on `Node::weight`.
// Since `priority_queue` is a max-heap, this comparator is reversed.
struct ByWeightReversed {
  bool operator()(const Node& left, const Node& right) const {
    return left.weight > right.weight;
  }
};

using NodeHeap = std::priority_queue<Node, std::vector<Node>, ByWeightReversed>;

void fill_leaves(NodeHeap& heap, const SymbolCounts& symbols) {
  for (const auto& [symbol, count] : symbols.counts) {
    heap.push(Node{
      .weight = count,
      .which = Node::Type::leaf,
      .leaf = symbol
    });
  }
}

void print_graph(std::ostream& out, NodeHeap& heap, const std::string& extra) {
  if (!extra.empty()) {
    // TODO: Maybe make it an isolated node.
    out << "# Input has extra trailing bytes: " << hexed(extra);
  }
  if (heap.empty()) {
    return;
  }
  out << "digraph {\n";
  InternalNodeID next_internal_node = 0;
  for (;;) {
    Node left = heap.top();
    out << "  " << left.name() << " [label=" << left.label_quoted() << "];\n";
    heap.pop();
    if (heap.empty()) {
      break;
    }
    Node right = heap.top();
    out << "  " << right.name() << " [label=" << right.label_quoted() << "];\n";
    Node parent = {
      .weight = left.weight + right.weight,
      .which = Node::Type::internal,
      .internal = next_internal_node++
    };
    out << "  " << parent.name() << " -> " << left.name() << " [label=\"0\"];\n";
    out << "  " << parent.name() << " -> " << right.name() << " [label=\"1\"];\n";
    heap.pop();
    heap.push(std::move(parent));
  }
  out << "}\n";
}

int main() {
  SymbolCounts symbols;
  std::cin >> symbols;
  if (std::cin.bad()) {
    return 1;
  }
  NodeHeap heap;
  fill_leaves(heap, symbols);
  print_graph(std::cout, heap, symbols.extra);
}
