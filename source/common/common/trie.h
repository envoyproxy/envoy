#pragma once

namespace {
// Because vector is efficient when removing elements from the back but paths
// generally have the most significant components at the front, it's useful to
// reverse the components vector before processing
template <class T> inline void reverseComponentVector(std::vector<T>& vec) {
  std::reverse(vec.begin(), vec.end());
}
} // namespace

/**
 * This is a general purpose trie. Keys and Values can be arbitrary classes
 * provided they can be hashed. It supports both partial matches and exact
 * matches.
 *
 * Note that Value must have a conversion constructor that will take nullptr.
 */
// TODO(tschroed): It's possible for Key to be a different type from the
// internal path component. E.g. a uint32_t IPv4 prefix can be tokenized to
// uint8_t components. Likewise a string representation of same.
template <class Key, class Value, class Tokenizer> class TrieNode {
public:
  TrieNode(const Value& value) : value_(value) {}
  TrieNode() : TrieNode(nullptr) {}
  ~TrieNode() {}

  /**
   * Insert a value into the trie.
   * @param key the name associated with the value.
   * @param value the value to store on the node associated with name
   */
  void emplace(const Key& key, const Value& value);

  /**
   * Lookup a value by key.
   * @param key the path name to look up
   * @return a std::pair consisting of the value if found and a boolean
   *         indicating if the match was exact.
   */
  std::pair<Value*, bool> find(const Key& key) const;

private:
  std::pair<Value*, bool> find(std::vector<Key>& path_components) const;
  void emplace(std::vector<Key>& path_components, const Value& value);
  void set_value(const Value& value) { value_ = value; }

  // By holding pointers to the TriedNodes we don't have to worry that they're
  // incomplete types.
  std::unordered_map<Key, std::unique_ptr<TrieNode<Key, Value, Tokenizer>>> children_;
  // value_ is being held as an opaque value and mutating it doesn't mutate the
  // container.
  mutable Value value_;
  Tokenizer tokenizer_;
};

template <class Key, class Value, class Tokenizer>
void TrieNode<Key, Value, Tokenizer>::emplace(const Key& key, const Value& value) {
  std::vector<Key> path_components = tokenizer_.tokenize(key);
  reverseComponentVector(path_components);
  emplace(path_components, value);
}

// TODO(tschroed): Make this iterative rather than recursive.
template <class Key, class Value, class Tokenizer>
void TrieNode<Key, Value, Tokenizer>::emplace(std::vector<Key>& path_components,
                                              const Value& value) {
  if (path_components.empty()) {
    return;
  }
  Key name(path_components.back());
  path_components.pop_back();
  TrieNode<Key, Value, Tokenizer>* node = nullptr;
  auto it = children_.find(name);
  if (children_.find(name) == children_.end()) {
    node = new TrieNode<Key, Value, Tokenizer>(nullptr);
    children_.emplace(name, std::move(std::unique_ptr<TrieNode<Key, Value, Tokenizer>>(node)));
  } else {
    node = it->second.get();
  }
  if (path_components.empty()) {
    node->set_value(value);
  } else {
    node->emplace(path_components, value);
  }
}

template <class Key, class Value, class Tokenizer>
std::pair<Value*, bool> TrieNode<Key, Value, Tokenizer>::find(const Key& key) const {
  std::vector<Key> path_components = tokenizer_.tokenize(key);
  reverseComponentVector(path_components);
  return find(path_components);
}

// TODO(tschroed): Make this iterative rather than recursive.
template <class Key, class Value, class Tokenizer>
std::pair<Value*, bool>
TrieNode<Key, Value, Tokenizer>::find(std::vector<Key>& path_components) const {
  if (path_components.empty()) {
    // An exact match requires that we have a non-false value at the very end of
    // our path traversal.
    return std::make_pair(&value_, value_ ? true : false);
  }
  bool exact_match = false;
  Value* value = &value_;
  Key component(path_components.back());
  path_components.pop_back();
  auto it = children_.find(component);
  if (it != children_.end()) {
    const TrieNode<Key, Value, Tokenizer>* node = it->second.get();
    std::pair<Value*, bool> retval = node->find(path_components);
    if (*retval.first) {
      value = retval.first;
      exact_match = retval.second;
    }
  }
  return std::make_pair(value, exact_match);
}
