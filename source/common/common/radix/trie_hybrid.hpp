//
// Created by Ashesh Vidyut on 22/03/25.
//

#ifndef TRIE_HYBRID_H
#define TRIE_HYBRID_H

#include "tree.hpp"
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
    Tree<K, T> trie_tree;      // For keys <= 8
    Tree<K, T> radix_tree;     // For keys > 8

public:
    TrieHybrid() = default;

    // Insert a key-value pair
    std::tuple<TrieHybrid<K, T>, std::optional<T>, bool> insert(const K& k, const T& v) {
        if (k.size() <= 8) {
            // Use trie for keys <= 8
            auto [newTrie, oldVal, didUpdate] = trie_tree.insert(k, v);
            trie_tree = newTrie;
            return {*this, oldVal, didUpdate};
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
            // Use trie for keys <= 8
            auto [newTrie, oldVal, didUpdate] = trie_tree.del(k);
            trie_tree = newTrie;
            return {*this, oldVal, didUpdate};
        } else {
            // Use radix tree for keys > 8
            auto [newRadix, oldVal, didUpdate] = radix_tree.del(k);
            radix_tree = newRadix;
            return {*this, oldVal, didUpdate};
        }
    }

    // Get a value by key
    std::optional<T> Get(const K& search) const {
        // Try trie first for keys <= 8
        if (search.size() <= 8) {
            auto result = trie_tree.Get(search);
            if (result.has_value()) {
                return result;
            }
        }
        
        // Try radix tree for keys > 8
        if (search.size() > 8) {
            auto result = radix_tree.Get(search);
            if (result.has_value()) {
                return result;
            }
        }
        
        // If key size doesn't match the tree, try the other tree
        if (search.size() <= 8) {
            return radix_tree.Get(search);
        } else {
            return trie_tree.Get(search);
        }
    }

    // Longest prefix match - search both trie and radix tree
    LongestPrefixResult<K, T> LongestPrefix(const K& search) const {
        LongestPrefixResult<K, T> trie_result = trie_tree.LongestPrefix(search);
        LongestPrefixResult<K, T> radix_result = radix_tree.LongestPrefix(search);
        
        // Return the longer prefix match
        if (trie_result.found && radix_result.found) {
            if (trie_result.key.size() >= radix_result.key.size()) {
                return trie_result;
            } else {
                return radix_result;
            }
        } else if (trie_result.found) {
            return trie_result;
        } else if (radix_result.found) {
            return radix_result;
        } else {
            T zero{};
            return {K{}, zero, false};
        }
    }

    // Find all matching prefixes - search both trie and radix tree
    std::vector<std::pair<K, T>> findMatchingPrefixes(const K& searchKey) const {
        std::vector<std::pair<K, T>> results;
        
        // Get matches from trie
        auto trie_matches = trie_tree.findMatchingPrefixes(searchKey);
        results.insert(results.end(), trie_matches.begin(), trie_matches.end());
        
        // Get matches from radix tree
        auto radix_matches = radix_tree.findMatchingPrefixes(searchKey);
        results.insert(results.end(), radix_matches.begin(), radix_matches.end());
        
        return results;
    }

    // Get value at index
    std::tuple<K, T, bool> GetAtIndex(int index) const {
        // Try trie first
        auto trie_result = trie_tree.GetAtIndex(index);
        if (std::get<2>(trie_result)) {
            return trie_result;
        }
        
        // Try radix tree
        return radix_tree.GetAtIndex(index);
    }

    // Get total size
    int len() const {
        return trie_tree.len() + radix_tree.len();
    }

    // Get leaves in subtree
    int GetLeavesInSubtree() const {
        return trie_tree.GetLeavesInSubtree() + radix_tree.GetLeavesInSubtree();
    }

    // Get trie tree (for debugging/testing)
    const Tree<K, T>& getTrieTree() const {
        return trie_tree;
    }

    // Get radix tree (for debugging/testing)
    const Tree<K, T>& getRadixTree() const {
        return radix_tree;
    }
};

// Explicit template instantiations
extern template class TrieHybrid<std::string, std::string>;
extern template class TrieHybrid<std::string, int>;
extern template class TrieHybrid<std::string, double>;
extern template class TrieHybrid<std::vector<uint8_t>, std::string>;

#endif // TRIE_HYBRID_H 