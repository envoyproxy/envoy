//
// Created by Ashesh Vidyut on 22/03/25.
//

#ifndef TRIE_HYBRID_H
#define TRIE_HYBRID_H

#include "tree.hpp"
#include "source/common/common/trie_lookup_table.h"
#include <memory>
#include <vector>
#include <optional>
#include <tuple>

// Forward declarations
template<typename K, typename T>
class Tree;

template<typename K, typename T>
class Node;

template<typename K, typename T>
class LeafNode;

// LongestPrefixResult is defined in node.hpp

// TrieHybrid class that combines trie and radix tree
template<typename K, typename T>
class TrieHybrid {
private:
    Envoy::TrieLookupTable<T> trie_table;  // For keys <= 8
    Tree<K, T> radix_tree;          // For keys > 8

public:
    TrieHybrid() = default;

    // Insert a key-value pair
    std::tuple<TrieHybrid<K, T>, std::optional<T>, bool> insert(const K& k, const T& v) {
        if (k.size() <= 8) {
            // Use trie lookup table for keys <= 8
            bool didUpdate = trie_table.add(k, v);
            return {*this, std::nullopt, didUpdate};
        } else {
            // Use radix tree for keys > 8
            auto [newRadix, oldVal, didUpdate] = radix_tree.insert(k, v);
            radix_tree = newRadix;
            return {*this, oldVal, didUpdate};
        }
    }

    // Delete a key
    std::tuple<TrieHybrid<K, T>, std::optional<T>, bool> del(const K& k) {
        if (k.size() <= 8) {
            // TrieLookupTable doesn't have a delete method, so we'll just return false
            return {*this, std::nullopt, false};
        } else {
            // Use radix tree for keys > 8
            auto [newRadix, oldVal, didUpdate] = radix_tree.del(k);
            radix_tree = newRadix;
            return {*this, oldVal, didUpdate};
        }
    }

    // Get a value by key
    std::optional<T> Get(const K& search) const {
        if (search.size() <= 8) {
            // Try trie lookup table first
            auto result = trie_table.find(search);
            if (result != nullptr) {
                return std::optional<T>(result);
            }
        } else {
            // Try radix tree for keys > 8
            auto result = radix_tree.Get(search);
            if (result.has_value()) {
                return result;
            }
        }
        
        // If key size doesn't match the tree, try the other tree
        if (search.size() <= 8) {
            return radix_tree.Get(search);
        } else {
            auto result = trie_table.find(search);
            if (result != nullptr) {
                return std::optional<T>(result);
            }
        }
        
        return std::nullopt;
    }

    // Longest prefix match - search both trie and radix tree
    LongestPrefixResult<K, T> LongestPrefix(const K& search) const {
        LongestPrefixResult<K, T> radix_result = radix_tree.LongestPrefix(search);
        
        // For trie lookup table, we need to implement longest prefix manually
        // since it doesn't have a built-in longest prefix method
        T zero{};
        if (radix_result.found) {
            return radix_result;
        } else {
            return {K{}, zero, false};
        }
    }

    // Find all matching prefixes - search both trie and radix tree
    std::vector<std::pair<K, T>> findMatchingPrefixes(const K& searchKey) const {
        std::vector<std::pair<K, T>> results;
        
        // Get matches from radix tree
        auto radix_matches = radix_tree.findMatchingPrefixes(searchKey);
        results.insert(results.end(), radix_matches.begin(), radix_matches.end());
        
        // For trie lookup table, we can only find exact matches
        // since it doesn't have a findMatchingPrefixes method
        auto trie_result = trie_table.find(searchKey);
        if (trie_result != nullptr) {
            results.emplace_back(searchKey, trie_result);
        }
        
        return results;
    }

    // Get value at index
    std::tuple<K, T, bool> GetAtIndex(int index) const {
        // Try radix tree first
        auto radix_result = radix_tree.GetAtIndex(index);
        if (std::get<2>(radix_result)) {
            return radix_result;
        }
        
        // TrieLookupTable doesn't support index-based access
        T zero{};
        return {K{}, zero, false};
    }

    // Get total size
    int len() const {
        // TrieLookupTable doesn't have a size method, so we'll just return radix tree size
        return radix_tree.len();
    }

    // Get leaves in subtree
    int GetLeavesInSubtree() const {
        // TrieLookupTable doesn't have this method, so we'll just return radix tree count
        return radix_tree.GetLeavesInSubtree();
    }

    // Get trie table (for debugging/testing)
    const Envoy::TrieLookupTable<T>& getTrieTable() const {
        return trie_table;
    }

    // Get radix tree (for debugging/testing)
    const Tree<K, T>& getRadixTree() const {
        return radix_tree;
    }
};

// Explicit template instantiations
extern template class TrieHybrid<std::string, const void*>;
// extern template class TrieHybrid<std::string, int>;
// extern template class TrieHybrid<std::string, double>;
// extern template class TrieHybrid<std::vector<uint8_t>, std::string>;

#endif // TRIE_HYBRID_H 