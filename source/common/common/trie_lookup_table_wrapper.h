#pragma once

#include "source/common/runtime/runtime_features.h"

// Feature flag to control which trie implementation to use
// If true: use new radix tree implementation
// If false: use old TrieLookupTable implementation
#define USE_RADIX_TREE_FOR_TRIE_LOOKUP "envoy.reloadable_features.use_radix_tree_for_trie_lookup"

namespace Envoy {

// Forward declaration
template <class Value>
class TrieLookupTable;

// Wrapper class that conditionally uses either TrieLookupTable or Tree
template <class Value>
class TrieLookupTableWrapper {
public:
  TrieLookupTableWrapper() {
    if (Runtime::runtimeFeatureEnabled(USE_RADIX_TREE_FOR_TRIE_LOOKUP)) {
      use_radix_tree_ = true;
    } else {
      use_radix_tree_ = false;
    }
  }

  // Add a key-value pair to the trie
  bool add(const std::string& key, Value value, bool overwrite = true) {
    if (use_radix_tree_) {
      return addToRadixTree(key, std::move(value), overwrite);
    } else {
      return addToTrieLookupTable(key, std::move(value), overwrite);
    }
  }

  // Find a value by key (exact match)
  Value* find(const std::string& key) {
    if (use_radix_tree_) {
      return findInRadixTree(key);
    } else {
      return findInTrieLookupTable(key);
    }
  }

  const Value* find(const std::string& key) const {
    if (use_radix_tree_) {
      return findInRadixTree(key);
    } else {
      return findInTrieLookupTable(key);
    }
  }

  // Find all matching prefixes for a given key
  std::vector<Value> findMatchingPrefixes(const std::string& key) const {
    if (use_radix_tree_) {
      return findMatchingPrefixesInRadixTree(key);
    } else {
      return findMatchingPrefixesInTrieLookupTable(key);
    }
  }

  // Get the size of the trie
  size_t size() const {
    if (use_radix_tree_) {
      return sizeOfRadixTree();
    } else {
      return sizeOfTrieLookupTable();
    }
  }

  // Clear all entries
  void clear() {
    if (use_radix_tree_) {
      clearRadixTree();
    } else {
      clearTrieLookupTable();
    }
  }

private:
  bool use_radix_tree_;

  // Original TrieLookupTable implementation
  TrieLookupTable<Value> trie_lookup_table_;

  // Radix Tree implementation (conditionally included)
  struct RadixTreeImpl;
  std::unique_ptr<RadixTreeImpl> radix_tree_;

  // TrieLookupTable methods
  bool addToTrieLookupTable(const std::string& key, Value value, bool overwrite) {
    return trie_lookup_table_.add(key, std::move(value), overwrite);
  }

  Value* findInTrieLookupTable(const std::string& key) {
    // TrieLookupTable::find returns by value, so we can't return a pointer
    // This is a limitation of the current API
    return nullptr;
  }

  const Value* findInTrieLookupTable(const std::string& key) const {
    return const_cast<TrieLookupTableWrapper*>(this)->findInTrieLookupTable(key);
  }

  std::vector<Value> findMatchingPrefixesInTrieLookupTable(const std::string& key) const {
    auto result = trie_lookup_table_.findMatchingPrefixes(key);
    return std::vector<Value>(result.begin(), result.end());
  }

  size_t sizeOfTrieLookupTable() const {
    return trie_lookup_table_.size();
  }

  void clearTrieLookupTable() {
    trie_lookup_table_.clear();
  }

  // Radix Tree methods (implemented in .cc file)
  bool addToRadixTree(const std::string& key, Value value, bool overwrite);
  Value* findInRadixTree(const std::string& key);
  const Value* findInRadixTree(const std::string& key) const;
  std::vector<Value> findMatchingPrefixesInRadixTree(const std::string& key) const;
  size_t sizeOfRadixTree() const;
  void clearRadixTree();
};

} // namespace Envoy 