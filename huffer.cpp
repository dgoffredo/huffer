#include "input_bit_stream.h"
#include "output_bit_stream.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <istream>
#include <locale>
#include <memory>
#include <queue>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

// `symbol_size` is the size, in bytes, of each input symbol.
// It is at least one byte and at most eight bytes.
// When encoding, the value is set based on a command line option.
// When decoding, the value is read from the file header.
// `symbol_size` is used in the implementation of `class Symbol`.
std::size_t symbol_size = 1;

// `Symbol` is a fixed size chunk of the uncompressed input.
// Huffman coding works by choosing shorter code words for more frequent
// symbols, and longer code words for less frequent symbols.
class Symbol {
  std::array<char, 8> storage;
public:
  const char *data() const { return storage.data(); }
  char *data() { return storage.data(); }
  const char *begin() const { return data(); }
  char *begin() { return data(); }
  const char *end() const { return data() + symbol_size; }
  char *end() { return data() + symbol_size; }
  std::size_t size() const { return symbol_size; }
  char& operator[](std::size_t i) { return storage[i]; }
  char operator[](std::size_t i) const { return storage[i]; }
};

bool operator==(const Symbol& left, const Symbol& right) {
  return std::equal(left.begin(), left.end(), right.begin());
}

std::ostream& operator<<(std::ostream& stream, const Symbol& symbol) {
  return stream.write(symbol.data(), symbol.size());
}

struct HashSymbol {
  std::size_t operator()(Symbol symbol) const {
    return hash_bytes(symbol.data(), symbol.size());
  }

  static long hash_bytes(const char* begin, std::size_t size) {
    return std::use_facet<std::collate<char>>(std::locale::classic()).hash(begin, begin + size);
  }
};

struct SymbolInfo {
  // `frequency` is how often the symbol appears in the decoded file.
  // It's used during encoding and graphing.
  std::uint64_t frequency = 0;
  // `code_word` are the bits of the encoded version of the symbol.
  // It's used during encoding.
  std::vector<bool> code_word;
};

struct Symbols {
  // `info` maps each input symbol to information needed for encoding or
  // graphing.
  std::unordered_map<Symbol, SymbolInfo, HashSymbol> info;
  // `extra` is any trailing (unencoded) data. If the unencoded file's size is
  // not a multiple of the symbol size, then `extra` will contain the
  // remainder.
  // It's used during graphing.
  std::string extra;
  // `total_size` is the length, in bytes, of the entire input.
  std::uint64_t total_size = 0;
};

struct Node {
  // `weight` is the sum of all symbol frequencies in the subtree rooted at
  // this node.
  // It's used during encoding and graphing.
  std::uint64_t weight;
  // `type`, `leaf`, and `internal` form a discriminated union.
  // A `Node` is either a leaf node or an internal node.
  // A leaf node is just a `Symbol`.
  // An internal node contains pointers to its left and right subtrees.
  // The `left` subtree corresponds to a 0 bit in the code word, while
  // the `right` subtree corresponds to a 1 bit in the code word.
  // The subtrees of an internal node are never null.
  // An internal node also contains an integer ID, which is used during
  // graphing.
  enum class Type : bool {
    leaf,
    internal
  } type;
  union {
    Symbol leaf;
    struct {
      std::uint64_t id;
      Node *left;
      Node *right;
    } internal;
  };
};

// `Tree` is a deleting wrapper around a (root) `Node`.
// Destroying a `Node` does not destroy its children, but destroying a `Tree`
// destroys all descendant `Node`s.
struct NodeDeleter {
  void operator()(Node *root) const {
    if (!root) {
      return;
    }
    std::vector<Node*> stack;
    stack.push_back(root);
    do {
      Node *node = stack.back();
      stack.pop_back();
      if (node->type == Node::Type::internal) {
        stack.push_back(node->internal.left);
        stack.push_back(node->internal.right);
      }
      delete node;
    } while (!stack.empty());
  }
};

using Tree = std::unique_ptr<Node, NodeDeleter>;

Symbols read_symbols(std::istream& in) {
  Symbols symbols;

  Symbol buffer;
  for (;;) {
    in.read(buffer.data(), buffer.size());
    const int count = in.gcount();
    symbols.total_size += count;
    if (count < int(symbol_size)) {
      symbols.extra.assign(buffer.data(), count);
      break;
    }
    ++symbols.info[buffer].frequency;
  }

  return symbols;
}

// We want our heap (`priority_queue`) to be a min-heap on `Node::weight`.
// Since `priority_queue` is a max-heap, this comparator is reversed.
struct ByWeightReversed {
  bool operator()(const Node *left, const Node *right) const {
    return left->weight > right->weight;
  }
};

Tree build_tree(const Symbols& symbols) {
  std::priority_queue<Node*, std::vector<Node*>, ByWeightReversed> heap;

  // First the leaves.
  for (const auto& [symbol, info] : symbols.info) {
    heap.push(new Node{
      .weight = info.frequency,
      .type = Node::Type::leaf,
      .leaf = symbol
    });
  }

  // Build up the tree's internal nodes greedily, always taking the two lowest
  // weighted nodes to create a new node.
  std::uint64_t next_node_id = 1;
  while (heap.size() > 1) {
    Node *left = heap.top();
    heap.pop();
    Node *right = heap.top();
    heap.pop();
    heap.push(new Node{
      .weight = left->weight + right->weight,
      .type = Node::Type::internal,
      .internal = {
        .id = next_node_id++,
        .left = left,
        .right = right
      }
    });
  }

  if (heap.empty()) {
    return nullptr;
  }
  return Tree(heap.top());
}

void build_code_words(Symbols& symbols, const Node *root) {
  if (!root) {
    return;
  }
  // Corner case: If there's only one symbol, then it codes to "0".
  if (root->type == Node::Type::leaf) {
    std::vector<bool> zero;
    zero.push_back(false);
    symbols.info.find(root->leaf)->second.code_word = zero;
    return;
  }

  struct Ancestor {
    const Node *node;
    std::vector<bool> prefix;
  };
  std::vector<Ancestor> ancestors;
  ancestors.push_back({.node = root, .prefix = std::vector<bool>{}});
  do {
    auto [parent, prefix] = std::move(ancestors.back());
    ancestors.pop_back();

    const Node *left = parent->internal.left;
    const Node *right = parent->internal.right;
    std::vector<bool> left_prefix = prefix;
    left_prefix.push_back(false);
    std::vector<bool> right_prefix = std::move(prefix);
    right_prefix.push_back(true);

    if (left->type == Node::Type::leaf) {
      symbols.info[left->leaf].code_word = std::move(left_prefix);
    } else {
      ancestors.push_back({
        .node = left,
        .prefix = std::move(left_prefix)
      });
    }

    if (right->type == Node::Type::leaf) {
      symbols.info[right->leaf].code_word = std::move(right_prefix);
    } else {
      ancestors.push_back({
        .node = right,
        .prefix = std::move(right_prefix)
      });
    }
  } while (!ancestors.empty());
}

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
DubscapedPrinter<Chars> dubscaped(const Chars& chars) {
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
HexedPrinter<Chars> hexed(const Chars& chars) {
  return HexedPrinter<Chars>{.chars = &chars};
}

struct LabelQuotedPrinter {
  const Node& node;

  friend std::ostream& operator<<(std::ostream& out, LabelQuotedPrinter printer) {
    const Node& node = printer.node;
    if (node.type == Node::Type::leaf) {
      out << "\"\\\"" << dubscaped(node.leaf) << "\\\" (" << std::dec << node.weight << ")\"";
    } else {
      out << "\"(" << std::dec << node.weight << ")\"";
    }
    return out;
  }
};

LabelQuotedPrinter label_quoted(const Node& node) {
  return LabelQuotedPrinter{.node = node};
}

struct NamePrinter {
  const Node& node;

  friend std::ostream& operator<<(std::ostream& out, NamePrinter printer) {
    const Node& node = printer.node;
    if (node.type == Node::Type::leaf) {
      out << "leaf_0x" << hexed(node.leaf);
    } else {
      out << "internal_" << std::dec << static_cast<std::uint64_t>(node.internal.id);
    }
    return out;
  }
};

NamePrinter name(const Node& node) {
  return NamePrinter{.node = node};
}

void graph_tree(std::ostream& out, const Node& root, const std::string& extra) {
  const char *indent = "  ";
  out << "digraph {\n";

  if (!extra.empty()) {
    // unattached node for any trailing data
    out << indent << "extra [label=\"\\\"" << dubscaped(extra) << "\\\" (extra)\"];\n";
  }

  std::vector<const Node*> stack;
  stack.push_back(&root);
  do {
    const Node& node = *stack.back();
    stack.pop_back();
    out << indent << name(node) << " [label=" << label_quoted(node) << "];\n";
    if (node.type == Node::Type::leaf) {
      continue;
    }
    out << indent << name(node) << " -> " << name(*node.internal.left) << " [label=\"0\"];\n";
    out << indent << name(node) << " -> " << name(*node.internal.right) << " [label=\"1\"];\n";
    stack.push_back(node.internal.left);
    stack.push_back(node.internal.right);
  } while (!stack.empty());

  out << "}\n";
}

OutputBitStream& operator<<(OutputBitStream& out, const Symbol& symbol) {
  for (char byte : symbol) {
    out << byte;
  }
  return out;
}

void write_tree(OutputBitStream& out, const Node *root) {
  if (root == nullptr) {
    return;
  }

  // The format for a node is <type><payload>.
  // <type> is a single bit: 0 means "internal" and 1 means "leaf."
  // <payload> for a leaf is the symbol.
  // <payload> for a node is the left child followed by the right child.
  std::vector<const Node*> stack;
  stack.push_back(root);
  do {
    const Node *node = stack.back();
    stack.pop_back();
    if (node->type == Node::Type::leaf) {
      out << true;
      out << node->leaf;
      continue;
    }
    out << false;
    stack.push_back(node->internal.right);
    stack.push_back(node->internal.left);
  } while (!stack.empty());
}

InputBitStream& operator>>(InputBitStream& in, Symbol& symbol) {
  for (char& byte : symbol) {
    in >> byte;
  }
  return in;
}

Tree read_tree(InputBitStream& in) {
  // If an error occurs, return `nullptr`.

  // The format for a node is <type><payload>.
  // <type> is a single bit: 0 means "internal" and 1 means "leaf."
  // <payload> for a leaf is the symbol.
  // <payload> for a node is the left child followed by the right child.
  bool is_leaf;
  in >> is_leaf;
  if (!in) {
    return nullptr;
  }
#pragma GCC diagnostic push
  // You're wrong, GCC. You're WRONG.
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
  if (is_leaf) {
#pragma GCC diagnostic pop
    // The root is a leaf.
    Symbol symbol;
    in >> symbol;
    if (!in) {
      return nullptr;
    }
    return Tree(new Node{
      .weight = 0, // unused
      .type = Node::Type::leaf,
      .leaf = symbol
    });
  }

  // The root is an internal node.
  Tree root;
  std::uint64_t next_node_id = 1;
  root.reset(new Node{
    .weight = 0,
    .type = Node::Type::internal,
    .internal = {
      .id = next_node_id++,
      .left = nullptr, // TBD
      .right = nullptr // TBD
    }
  });
  std::vector<Node*> ancestors;
  ancestors.push_back(root.get());
  do {
    Node *parent = ancestors.back();
    assert(parent->type == Node::Type::internal);
    if (parent->internal.left && parent->internal.right) {
      ancestors.pop_back();
      continue;
    }

    bool is_leaf;
    in >> is_leaf;
    if (!in) {
      return nullptr;
    }

    Node *node;
#pragma GCC diagnostic push
    // You're wrong, GCC. You're WRONG.
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    if (is_leaf) {
#pragma GCC diagnostic pop
      Symbol symbol;
      in >> symbol;
      if (!in) {
        return nullptr;
      }
      node = new Node{
        .weight = 0, // unused
        .type = Node::Type::leaf,
        .leaf = symbol
      };
    } else {
      // It's an internal node.
      node = new Node{
        .weight = 0, // unused
        .type = Node::Type::internal,
        .internal = {
          .id = next_node_id++,
          .left = nullptr, // TBD
          .right = nullptr // TBD
        }
      };
      ancestors.push_back(node);
    }

    if (parent->internal.left == nullptr) {
      parent->internal.left = node;
    } else {
      parent->internal.right = node;
    }
  } while (!ancestors.empty());

  return root;
}

int main_graph(const char *input_path, std::ostream& out) {
  std::ifstream fin;
  std::streambuf *buf;
  if (input_path) {
    fin.open(input_path);
    if (!fin) {
      return 1;
    }
    buf = fin.rdbuf();
  } else {
    buf = std::cin.rdbuf();
  }
  std::istream in{buf};

  Symbols symbols = read_symbols(in);
  Tree tree = build_tree(symbols);
  if (tree == nullptr) {
    return 0;
  }
  graph_tree(out, *tree, symbols.extra);
  return 0;
}

int main_encode(const char *input_path, std::ostream& out) {
  std::ifstream in{input_path};
  if (!in) {
    return 1;
  }

  Symbols symbols = read_symbols(in);
  Tree tree = build_tree(symbols);
  build_code_words(symbols, tree.get());

  // Write the header and the tree.
  out << "huffer1" << '\0';
  OutputBitStream bitout{*out.rdbuf()};
  bitout << std::bitset<64>{symbols.total_size} << std::bitset<3>{symbol_size - 1};
  write_tree(bitout, tree.get());

  // Start from the beginning of input again, and encode it.
  in.clear();
  in.seekg(0, std::ios::beg);
  Symbol buffer;
  for (;;) {
    in.read(buffer.data(), buffer.size());
    const int count = in.gcount();
    if (count < int(symbol_size)) {
      // We reached the "extra." Copy it verbatim (unencoded).
      for (int i = 0; i < count; ++i) {
        bitout << buffer[i];
      }
      break;
    }

    bitout << symbols.info[buffer].code_word;
  }

  return 0;
}

int main_decode(const char *input_path, std::ostream& out) {
  std::ifstream fin;
  std::streambuf *buf;
  if (input_path) {
    fin.open(input_path);
    if (!fin) {
      return 1;
    }
    buf = fin.rdbuf();
  } else {
    buf = std::cin.rdbuf();
  }
  std::istream in{buf};

  // Read the header.
  char magic[8] = {};
  in.read(magic, sizeof magic);
  if (!in) {
    return 2;
  }
  const char expected[] = {'h', 'u', 'f', 'f', 'e', 'r', '1', '\0'};
  if (!std::equal(magic, magic + sizeof magic, expected)) {
    return 3;
  }
  InputBitStream bitin{*in.rdbuf()};
  std::bitset<64> raw_total_size;
  bitin >> raw_total_size;
  if (!bitin) {
    return 4;
  }
  std::bitset<3> raw_symbol_size;
  bitin >> raw_symbol_size;
  if (!bitin) {
    return 5;
  }
  const std::uint64_t total_size = raw_total_size.to_ullong();
  symbol_size = raw_symbol_size.to_ulong() + 1;
  if (symbol_size > 8) {
    return 6;
  }

  if (total_size == 0) {
    return 0; // empty file
  }

  Tree tree = read_tree(bitin);
  // `expanded_size` is the length of the decoded output, excluding any
  // "extra."
  const std::uint64_t expanded_size = total_size - (total_size % symbol_size);
  for (std::uint64_t bytes_written = 0; bytes_written < expanded_size; bytes_written += symbol_size) {
    // Read bits until we hit a leaf.
    const Node *node = tree.get();
    for (;;) {
      bool bit;
      bitin >> bit;
      if (!bitin) {
        return 7;
      }
      if (node->type == Node::Type::leaf) {
        // The root is a leaf, so just copy it over.
        assert(!bit);
        out << node->leaf;
        break;
      }
      node = bit ? node->internal.right : node->internal.left;
      if (node->type == Node::Type::leaf) {
        out << node->leaf;
        break;
      }
    }
  }

  // Copy over the remaining "extra" verbatim.
  char byte;
  while (bitin >> byte) {
    out.put(byte);
  }

  return 0;
}

std::ostream& usage(std::ostream& out) {
  return out <<
    "huffer - Huffman coding based data compression\n"
    "\n"
    "usage:\n"
    "\n"
    "  huffer --help\n"
    "  huffer -h\n"
    "    Print this message to standard output.\n"
    "\n"
    "  huffer encode [--symbol-size=N] FILE\n"
    "  huffer compress [--symbol-size=N] FILE\n"
    "    Compress the specified FILE using a symbol size of N,\n"
    "    or 1 by default. Print the compressed data to standard\n"
    "    output.\n"
    "\n"
    "  huffer decode [FILE]\n"
    "  huffer decompress [FILE]\n"
    "    Decompress the optionally specified FILE. Print the\n"
    "    decompressed data to standard output. If FILE is not\n"
    "    specified, then read from standard input.\n"
    "\n"
    "  huffer graph [--symbol-size=N] [FILE]\n"
    "    Create a Huffman tree of the specified FILE using\n"
    "    a symbol size of N, or 1 by default. Print the graph to\n"
    "    standard output in dot (Graphviz) format. If FILE is not\n"
    "    specified, then read from standard input.\n"
    "\n";
}

int parse_command_line(
    const char * const *argv,
    bool& help,
    std::string& command,
    const char *& file) {
  help = false;

  const char *const *arg = argv + 1;
  if (!*arg) {
    usage(std::cerr) << "Not enough arguments.\n";
    return -1;
  }

  command  = *arg;
  ++arg;

  if (command == "-h" || command == "--help") {
    help = true;
    usage(std::cout);
    return 0;
  }

  if (*arg && (command == "encode" || command == "compress" || command == "graph")) {
    // Possibly read `--symbol-size=N`.
    std::string_view chunk = *arg;
    const std::string_view prefix = "--symbol-size=";
    if (chunk.starts_with(prefix)) {
      chunk.remove_prefix(prefix.size());
      const auto result = std::from_chars(chunk.data(), chunk.data() + chunk.size(), symbol_size);
      if (result.ec != std::errc{} || result.ptr != chunk.data() + chunk.size()) {
        usage(std::cerr) << "Invalid symbol size: " << chunk << '\n';
        return -3;
      }
      ++arg;
    }
  }

  file = *arg;
  if (!file && (command == "encode" || command == "compress")) {
    usage(std::cerr) << command << " requires a FILE argument.\n";
    return -4;
  }

  return 0;
}

int main(int /*argc*/, char *argv[]) {
  bool help;
  std::string command;
  const char *file;
  if (int rc = parse_command_line(argv, help, command, file)) {
    return rc;
  }
  if (help) {
    return 0;
  }

  if (command == "encode" || command == "compress") {
    return main_encode(file, std::cout);
  }
  if (command == "decode" || command == "decompress") {
    return main_decode(file, std::cout);
  }
  if (command == "graph") {
    return main_graph(file, std::cout);
  }

  usage(std::cerr) << "Unknown command: " << command << '\n';
  return -5;
}
