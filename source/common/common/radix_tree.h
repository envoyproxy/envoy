#pragma once

#include <algorithm>
#include <vector>

#include "envoy/common/optref.h"

#include "source/common/common/assert.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {

/**
 * A radix tree implementation for efficient prefix-based lookups.
 *
 * Template parameter Value must be default-constructible and moveable.
 */
template <class Value> class RadixTree {
public:
  /**
   * Adds an entry to the RadixTree at the given key.
   * @param key the key used to add the entry.
   * @param value the value to be associated with the key.
   * @param overwrite_existing will overwrite the value when the value for a given key already
   * exists.
   * @return false when a value already exists for the given key and overwrite_existing is false.
   */
  bool add(absl::string_view key, Value value, bool overwrite_existing = true) {
    // Check if the key already exists.
    Value existing;
    bool found = root_.findRecursive(key, existing);

    // If a value exists and we shouldn't overwrite, return false.
    if (found && !overwrite_existing) {
      return false;
    }

    root_.insert(key, std::move(value));
    return true;
  }

  /**
   * Finds the entry associated with the key.
   * @param key the key used to find.
   * @return the Value associated with the key, or an empty-initialized Value
   *         if there is no matching key.
   */
  Value find(absl::string_view key) const {
    Value result;
    if (root_.findRecursive(key, result)) {
      return result;
    }
    return Value{};
  }

  /**
   * Returns the set of entries that are prefixes of the specified key, longest last.
   * Complexity is O(min(longest key prefix, key length)).
   * @param key the key used to find.
   * @return a vector of values whose keys are a prefix of the specified key, longest last.
   */
  absl::InlinedVector<Value, 4> findMatchingPrefixes(absl::string_view key) const {
    absl::InlinedVector<Value, 4> result;
    absl::string_view search = key;
    const RadixTreeNode* node = &root_;

    // Special case: if searching for empty string, check root node.
    if (search.empty()) {
      if (hasValue(*node)) {
        result.push_back(node->value_);
      }
      return result;
    }

    while (true) {
      // Check if current node has a value (is a leaf) and we've consumed some prefix.
      if (hasValue(*node)) {
        result.push_back(node->value_);
      }

      // Check for key exhaustion.
      if (search.empty()) {
        break;
      }

      // Look for an edge.
      uint8_t first_char = static_cast<uint8_t>(search[0]);
      auto child = node->getChild(first_char);
      if (!child) {
        break;
      }

      const RadixTreeNode& child_node = *child;
      node = &child_node;

      // Consume the search prefix.
      if (search.size() < child->prefix_.size() ||
          search.substr(0, child->prefix_.size()) != child->prefix_) {
        break;
      }
      // Consume the search prefix.
      search = search.substr(child->prefix_.size());
    }

    return result;
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
    absl::string_view search = key;
    const RadixTreeNode* node = &root_;
    const RadixTreeNode* last_node_with_value = nullptr;

    while (true) {
      // Look for a leaf node.
      if (hasValue(*node)) {
        last_node_with_value = node;
      }

      // Check for key exhaustion.
      if (search.empty()) {
        break;
      }

      // Look for an edge.
      uint8_t first_char = static_cast<uint8_t>(search[0]);
      auto child = node->getChild(first_char);
      if (!child) {
        break;
      }

      const RadixTreeNode& child_node = *child;
      node = &child_node;

      // Consume the search prefix.
      if (search.size() < child->prefix_.size() ||
          search.substr(0, child->prefix_.size()) != child->prefix_) {
        break;
      }
      // Consume the search prefix.
      search = search.substr(child->prefix_.size());
    }

    // Return the value from the last node that had a value, or empty value if none found.
    if (last_node_with_value != nullptr) {
      return last_node_with_value->value_;
    }
    return Value{};
  }

private:
  static constexpr int32_t NoNode = -1;

  /**
   * Internal node structure for the radix tree.
   */
  struct RadixTreeNode {
    std::string prefix_;
    Value value_{};

    // Hash map for O(1) child lookup by first character.
    absl::flat_hash_map<uint8_t, RadixTreeNode> children_;

    /**
     * Insert a key-value pair into this node.
     * @param search the remaining search key.
     * @param value the value to insert.
     */
    void insert(absl::string_view search, Value value) {
      // Handle key exhaustion.
      if (search.empty()) {
        value_ = std::move(value);
        return;
      }

      // Look for the edge.
      uint8_t first_char = static_cast<uint8_t>(search[0]);
      auto child_it = children_.find(first_char);

      // No edge, create one.
      if (child_it == children_.end()) {
        // Create a new child node.
        RadixTreeNode new_child;
        new_child.prefix_ = std::string(search);
        new_child.value_ = std::move(value);

        // Add the child to the current node.
        children_[first_char] = std::move(new_child);
        return;
      }

      // Get the child node.
      RadixTreeNode& child = child_it->second;

      // Determine longest prefix length of the search key on match.
      size_t cpl = commonPrefixLength(search, child.prefix_);
      if (cpl == child.prefix_.size()) {
        // The search key is longer than the child prefix, continue down.
        absl::string_view remaining_search = search.substr(cpl);
        child.insert(remaining_search, std::move(value));
        return;
      }

      // Split the node. We create a new intermediate node.
      RadixTreeNode split_node;
      split_node.prefix_ = std::string(search.substr(0, cpl));

      // Update the child's prefix.
      child.prefix_ = std::string(child.prefix_.substr(cpl));

      // If the search key is exactly the common prefix, set the value on the split node.
      if (cpl == search.size()) {
        split_node.value_ = std::move(value);
      } else {
        // Create a new leaf for the current key.
        RadixTreeNode new_leaf;
        new_leaf.prefix_ = std::string(search.substr(cpl));
        new_leaf.value_ = std::move(value);
        split_node.children_[static_cast<uint8_t>(new_leaf.prefix_[0])] = std::move(new_leaf);
      }

      // Add the child to the split node.
      split_node.children_[static_cast<uint8_t>(child.prefix_[0])] = std::move(child);

      // Replace the original child with the split node.
      children_[first_char] = std::move(split_node);
    }

    /**
     * Recursive helper for find operation.
     * @param search the remaining search key.
     * @param result the value to return if found.
     * @return true if the key was found, false otherwise.
     */
    bool findRecursive(absl::string_view search, Value& result) const {
      if (search.empty()) {
        if (hasValue(*this)) {
          result = value_;
          return true;
        }
        return false;
      }

      uint8_t first_char = static_cast<uint8_t>(search[0]);
      auto child_it = children_.find(first_char);
      if (child_it == children_.end()) {
        return false;
      }

      const RadixTreeNode& child = child_it->second;

      // Check if the child's prefix matches the search.
      if (search.size() >= child.prefix_.size() &&
          search.substr(0, child.prefix_.size()) == child.prefix_) {
        absl::string_view new_search = search.substr(child.prefix_.size());
        return child.findRecursive(new_search, result);
      }

      return false;
    }

    /**
     * Get a child node by character key.
     * @param char_key the character to look up.
     * @return optional reference to the child node.
     */
    Envoy::OptRef<const RadixTreeNode> getChild(uint8_t char_key) const {
      auto it = children_.find(char_key);
      if (it != children_.end()) {
        return {it->second};
      }
      return {};
    }
  };

  /**
   * Find the longest common prefix between two strings.
   * @param a first string.
   * @param b second string.
   * @return length of the common prefix.
   */
  static size_t commonPrefixLength(absl::string_view a, absl::string_view b) {
    size_t len = std::min(a.size(), b.size());
    for (size_t i = 0; i < len; i++) {
      if (a[i] != b[i]) {
        return i;
      }
    }
    return len;
  }

  /**
   * Check if a node has a value (is a leaf node).
   * @param node the node to check.
   * @return true if the node has a value.
   */
  static bool hasValue(const RadixTreeNode& node) {
    // For pointer types, check if the pointer is not null.
    if constexpr (std::is_pointer_v<Value>) {
      return node.value_ != nullptr;
    } else {
      return static_cast<bool>(node.value_);
    }
  }

  // Root node of the radix tree.
  RadixTreeNode root_;
};

} // namespace Envoy
