#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

template <class Key, class Value, class Tokenizer> class TrieNode {
public:
  TrieNode(Value value) : value_(value) {}
  TrieNode() : TrieNode(nullptr) {}
  ~TrieNode() {}
  void insert(const Key& key, Value value);
  Value match(const Key& key) const;

private:
  Value match(std::vector<Key>& path_components) const;
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
Value TrieNode<Key, Value, Tokenizer>::match(const Key& key) const {
  std::vector<Key> path_components = tokenizer_.tokenize(key);
  return match(path_components);
}

template <class Key, class Value, class Tokenizer>
Value TrieNode<Key, Value, Tokenizer>::match(std::vector<Key>& path_components) const {
  if (path_components.size() == 0) {
    return nullptr;
  }
  Value value = value_;
  Key component = path_components[0];
  path_components.erase(path_components.begin());
  if (children_.find(component) != children_.end()) {
    const TrieNode<Key, Value, Tokenizer>* node = children_.find(component)->second.get();
    Value new_value = node->match(path_components);
    if (new_value) {
      value = new_value;
    }
  }
  return value;
}
