#pragma once

#include <vector>

#include "source/common/common/assert.h"

#include "absl/strings/string_view.h"

namespace Envoy {

/**
 * A trie used for faster lookup with lookup time at most equal to the size of the key.
 *
 * Type of Value must be empty-constructible and moveable, e.g. smart pointers and POD types.
 */
template <class Value> class TrieLookupTable {
  static constexpr int32_t NoNode = -1;
  // A TrieNode aims to be a good balance of performant and
  // space-efficient, by allocating a vector the size of the range of children
  // the node contains. This should be good for most use-cases.
  //
  // For example, a node with children 'a' and 'z' will contain a vector of
  // size 26, containing two values and 24 nulls. A node with only one
  // child will contain a vector of size 1. A node with no children will
  // contain an empty vector.
  //
  // Compared to allocating 256 entries for every node, this makes insertions
  // a little bit inefficient (especially insertions in reverse order), but
  // trie lookups remain O(length-of-longest-matching-prefix) with just a
  // couple of very cheap operations extra per step.
  //
  // By size, having 256 entries for every node makes each node's overhead
  // (excluding values) consume 8KB; even a trie containing only a single
  // prefix "foobar" consumes 56KB.
  // Using ranged vectors like this makes a single prefix "foobar" consume
  // less than 20 bytes per node, for a total of less than 0.14KB.
  //
  // Using indices instead of pointers helps keep the bulk of the data
  // localized, and prevents recursive deletion which can provoke a stack
  // overflow.
  struct TrieNode {
    Value value_{};
    // Vector of indices into nodes_, where [0] maps to min_child_key_.
    // NoNode will be in any index where there is not a child.
    std::vector<int32_t> children_;
    uint8_t min_child_key_{0};
  };

  /**
   * Get the index of the node that the branch whose key is `char_key` from the
   * node indexed by `current` leads to.
   * @param current the index of the node to follow a branch from.
   * @param char_key the one-byte key of the branch to be followed.
   */
  int32_t getChildIndex(int32_t current, uint8_t char_key) const {
    ASSERT(current >= 0 && static_cast<size_t>(current) < nodes_.size());
    const TrieNode& node = nodes_[current];
    if (node.min_child_key_ > char_key || node.min_child_key_ + node.children_.size() <= char_key) {
      return NoNode;
    }
    return node.children_[char_key - node.min_child_key_];
  }
  int32_t getChildIndex(int32_t current, uint8_t char_key) {
    return std::as_const(*this).getChildIndex(current, char_key);
  }

  /**
   * Make the branch whose key is `char_key`, of the node indexed by `current`
   * point to node indexed by `child_index`.
   * @param current the index of the node whose child is to be updated.
   * @param char_key the one-byte key of the branch to be updated.
   * @param child_index the index of the node the branch will lead to.
   */
  void setChildIndex(int32_t current, uint8_t char_key, int32_t child_index) {
    ASSERT(current >= 0 && static_cast<size_t>(current) < nodes_.size());
    ASSERT(child_index >= 0 && static_cast<size_t>(child_index) < nodes_.size());
    TrieNode& node = nodes_[current];
    if (node.children_.empty()) {
      node.children_.reserve(1);
      node.children_.push_back(child_index);
      node.min_child_key_ = char_key;
      return;
    }
    if (char_key < node.min_child_key_) {
      std::vector<int32_t> new_children;
      new_children.reserve(node.min_child_key_ - char_key + node.children_.size());
      new_children.resize(node.min_child_key_ - char_key, NoNode);
      std::move(node.children_.begin(), node.children_.end(), std::back_inserter(new_children));
      new_children[0] = child_index;
      node.min_child_key_ = char_key;
      node.children_ = std::move(new_children);
      return;
    }
    if (char_key >= (node.min_child_key_ + node.children_.size())) {
      // Expand the vector forwards.
      node.children_.resize(char_key - node.min_child_key_ + 1, NoNode);
      // Fall through to "insert" behavior.
    }
    node.children_[char_key - node.min_child_key_] = child_index;
  }

public:
  /**
   * Adds an entry to the Trie at the given Key.
   * @param key the key used to add the entry.
   * @param value the value to be associated with the key.
   * @param overwrite_existing will overwrite the value when the value for a given key already
   * exists.
   * @return false when a value already exists for the given key.
   */
  bool add(absl::string_view key, Value value, bool overwrite_existing = true) {
    int32_t current = 0;
    for (uint8_t c : key) {
      int32_t next = getChildIndex(current, c);
      if (next == NoNode) {
        next = nodes_.size();
        nodes_.emplace_back();
        setChildIndex(current, c, next);
      }
      current = next;
    }
    if (nodes_[current].value_ && !overwrite_existing) {
      return false;
    }
    nodes_[current].value_ = std::move(value);
    return true;
  }

  /**
   * Finds the entry associated with the key.
   * @param key the key used to find.
   * @return the Value associated with the key, or an empty-initialized Value
   *         if there is no matching key.
   */
  Value find(absl::string_view key) const {
    int32_t current = 0;
    for (uint8_t c : key) {
      current = getChildIndex(current, c);
      if (current == NoNode) {
        return {};
      }
    }
    return nodes_[current].value_;
  }

  /**
   * Finds the entry with the longest key that is a prefix of the specified key.
   * Complexity is O(min(longest key prefix, key length)).
   * @param key the key used to find.
   * @return a value whose key is a prefix of the specified key. If there are
   *         multiple such values, the one with the longest key. If there are
   *         no keys that are a prefix of the input key, an empty-initialized Value.
   */
  Value findLongestPrefix(absl::string_view key) const {
    int32_t current = 0;
    int32_t result = 0;

    for (uint8_t c : key) {
      current = getChildIndex(current, c);

      if (current == NoNode) {
        return nodes_[result].value_;
      } else if (nodes_[current].value_) {
        result = current;
      }
    }
    return nodes_[result].value_;
  }

private:
  // Flat representation of the tree - each node has a vector of indices to its
  // child nodes.
  // Initialized with a single empty node as the root node.
  std::vector<TrieNode> nodes_ = {TrieNode()};
};

} // namespace Envoy
