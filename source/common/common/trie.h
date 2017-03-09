#pragma once

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * This is a general purpose trie. Keys and Values can be arbitrary classes
 * provided they can be hashed. It supports both partial matches and exact
 * matches.
 *
 * Note that Value must have a conversion constructor that will take nullptr.
 */
template <class Key, class Value, class Tokenizer> class TrieNode {
public:
  TrieNode(Value value) : value_(value) {}
  TrieNode() : TrieNode(nullptr) {}
  ~TrieNode() {}

  /**
   * Insert a value into the trie.
   * @param key the name associated with the value.
   * @param value the value to store on the node associated with name
   */
  void insert(const Key& key, Value value);

  /**
   * Lookup a value by key.
   * @param key the path name to look up
   * @return a std::pair consisting of the value if found and a boolean
   *         indicating if the match was exact.
   */
  std::pair<Value, bool> match(const Key& key) const;

private:
  std::pair<Value, bool> match(std::vector<Key>& path_components) const;
  void insert(std::vector<Key>& path_components, Value value);
  Value value() const { return value_; }
  void set_value(Value value) { value_ = value; }

  // By holding pointers to the TriedNodes we don't have to worry that they're
  // incomplete types.
  std::unordered_map<Key, std::unique_ptr<TrieNode<Key, Value, Tokenizer>>> children_;
  Value value_;
  Tokenizer tokenizer_;
};

template <class Key, class Value, class Tokenizer>
void TrieNode<Key, Value, Tokenizer>::insert(const Key& key, Value value) {
  std::vector<Key> path_components = tokenizer_.tokenize(key);
  insert(path_components, value);
}

template <class Key, class Value, class Tokenizer>
void TrieNode<Key, Value, Tokenizer>::insert(std::vector<Key>& path_components, Value value) {
  Key name(path_components[0]);
  path_components.erase(path_components.begin());
  if (children_.find(name) == children_.end()) {
    std::unique_ptr<TrieNode<Key, Value, Tokenizer>> node(
        new TrieNode<Key, Value, Tokenizer>(nullptr));
    children_[name] = std::move(node);
  }
  TrieNode<Key, Value, Tokenizer>* node = children_[name].get();
  if (path_components.size() == 0) {
    node->set_value(value);
  } else {
    node->insert(path_components, value);
  }
}

template <class Key, class Value, class Tokenizer>
std::pair<Value, bool> TrieNode<Key, Value, Tokenizer>::match(const Key& key) const {
  std::vector<Key> path_components = tokenizer_.tokenize(key);
  return match(path_components);
}

template <class Key, class Value, class Tokenizer>
std::pair<Value, bool>
TrieNode<Key, Value, Tokenizer>::match(std::vector<Key>& path_components) const {
  // An exact match requires that we have a non-false value at the very end of
  // our path traversal.
  if (path_components.size() == 0) {
    return std::make_pair(value_, value_ ? true : false);
  }
  bool exact_match = false;
  Value value = value_;
  Key component = path_components[0];
  path_components.erase(path_components.begin());
  if (children_.find(component) != children_.end()) {
    const TrieNode<Key, Value, Tokenizer>* node = children_.find(component)->second.get();
    std::pair<Value, bool> retval = node->match(path_components);
    if (retval.first) {
      value = retval.first;
      exact_match = retval.second;
    }
  }
  return std::make_pair(value, exact_match);
}
