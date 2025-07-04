//
// Created by Ashesh Vidyut on 22/03/25.
//

#ifndef ITERATOR_H
#define ITERATOR_H

#include "node.hpp"
#include <vector>
#include <memory>
#include <regex.h>
#include <unordered_set>

// Forward declarations
template<typename K, typename T>
class Node;

// Iterator result structure
template<typename K, typename T>
struct IteratorResult {
    K key;
    T val;
    bool found;
};

// Forward declare Iterator
template<typename K, typename T>
class Iterator;

// Helper function to check if one sequence is a prefix of another
template<typename K>
bool hasPrefix(const K& str, const K& prefix) {
    if (str.size() < prefix.size()) return false;
    return std::equal(prefix.begin(), prefix.end(), str.begin());
}

// ReverseIterator class for traversing nodes in reverse in-order
template<typename K, typename T>
class ReverseIterator {
private:
    std::shared_ptr<Node<K, T>> node;
    LeafNode<K, T>* iterLeafNode;
    int iterCounter;
    K key;

public:
    ReverseIterator(std::shared_ptr<Node<K, T>> n) : node(n) {
        if (node) {
            iterLeafNode = node->maxLeaf;
            iterCounter = node->leaves_in_subtree;
        } else {
            iterLeafNode = nullptr;
            iterCounter = 0;
        }
    }

    // Seeks the iterator to a given prefix and returns the watch channel
    void seekPrefixWatch(const K& prefix) {
        key = prefix;
        auto n = node;
        K search = prefix;
        iterLeafNode = node->maxLeaf;
        iterCounter = node->leaves_in_subtree;
        
        while (n) {
            // Check for key exhaustion
            if (search.empty()) {
                node = n;
                iterLeafNode = node->maxLeaf;
                iterCounter = node->leaves_in_subtree;
                return;
            }

            // Look for an edge
            auto nextNode = n->getEdge(search[0]);
            if (!nextNode) {
                node = nullptr;
                iterLeafNode = nullptr;
                iterCounter = 0;
                return;
            }

            // Consume the search prefix
            if (hasPrefix(search, nextNode->prefix)) {
                search = K(search.begin() + nextNode->prefix.size(), search.end());
            } else if (hasPrefix(nextNode->prefix, search)) {
                node = nextNode;
                iterLeafNode = node->maxLeaf;
                iterCounter = node->leaves_in_subtree;
                return;
            } else {
                node = nullptr;
                iterLeafNode = nullptr;
                iterCounter = 0;
                return;
            }
            
            n = nextNode;
        }
    }

    // Seeks the iterator to a given prefix
    void seekPrefix(const K& prefix) {
        seekPrefixWatch(prefix);
    }

    // Returns the previous element in reverse order
    IteratorResult<K, T> previous() {
        IteratorResult<K, T> result;
        result.found = false;

        if (iterCounter > 0 && iterLeafNode) {
            iterCounter--;
            
            result.key = iterLeafNode->key;
            result.val = iterLeafNode->val;
            result.found = true;
            iterLeafNode = iterLeafNode->prevLeaf;
            return result;
        }

        return result;
    }
};

// PrefixIterator class for walking down the tree following a path
template<typename K, typename T>
class PrefixIterator {
private:
    std::shared_ptr<Node<K, T>> rootNode;
    K searchPath;
    std::vector<std::shared_ptr<Node<K, T>>> nodeStack;
    size_t stackIndex;
    bool initialized;

public:
    PrefixIterator(std::shared_ptr<Node<K, T>> n, const K& path)
        : rootNode(n), searchPath(path), stackIndex(0), initialized(false) {}

    // Returns the next node in order along the path
    IteratorResult<K, T> next() {
        IteratorResult<K, T> result;
        result.found = false;

        // Initialize the path walk if not done yet
        if (!initialized) {
            initializePath();
            initialized = true;
        }

        // Return the next leaf from the stack
        while (stackIndex < nodeStack.size()) {
            auto currentNode = nodeStack[stackIndex];
            stackIndex++;

            if (currentNode->leaf) {
                result.key = currentNode->leaf->key;
                result.val = currentNode->leaf->val;
                result.found = true;
                return result;
            }
        }

        return result;
    }

private:
    void initializePath() {
        if (!rootNode) return;

        auto currentNode = rootNode;
        K remainingPath = searchPath;

        // Start with the root node
        nodeStack.push_back(currentNode);

        // Walk down the tree following the path
        while (currentNode && !remainingPath.empty()) {
            // Look for an edge that matches the next character
            auto nextNode = currentNode->getEdge(remainingPath[0]);

            if (!nextNode) {
                break;
            }

            // Check if the node's prefix matches the remaining path
            if (hasPrefix(remainingPath, nextNode->prefix)) {
                // Add this node to the stack
                nodeStack.push_back(nextNode);

                // Consume the prefix
                remainingPath = K(remainingPath.begin() + nextNode->prefix.size(), remainingPath.end());
                currentNode = nextNode;
            } else {
                break;
            }
        }
    }
};

// Iterator class
template<typename K, typename T>
class Iterator {
private:
    std::shared_ptr<Node<K, T>> node;
    LeafNode<K, T>* iterLeafNode;
    int iterCounter;
    K key;
    bool seekLowerBoundFlag;

public:
    Iterator(std::shared_ptr<Node<K, T>> n) : node(n), seekLowerBoundFlag(false) {
        if (node) {
            iterLeafNode = node->minLeaf;
            iterCounter = node->leaves_in_subtree;
        } else {
            iterLeafNode = nullptr;
            iterCounter = 0;
        }
    }

    // Range-based for loop operators
    std::pair<const K&, const T&> operator*() const {
        return {iterLeafNode->key, iterLeafNode->val};
    }

    Iterator& operator++() {
        if (iterCounter > 0 && iterLeafNode) {
            iterCounter--;
            iterLeafNode = iterLeafNode->nextLeaf;
        }
        return *this;
    }

    bool operator!=(const Iterator& other) const {
        return iterLeafNode != other.iterLeafNode;
    }

    // Seeks the iterator to a given prefix and returns the watch channel
    void seekPrefixWatch(const K& prefix) {
        // Wipe the stack
        seekLowerBoundFlag = false;
        key = prefix;
        auto n = node;
        K search = prefix;
        iterLeafNode = node->minLeaf;
        iterCounter = node->leaves_in_subtree;

        while (n) {
            // Check for key exhaustion
            if (search.empty()) {
                node = n;
                iterLeafNode = node->minLeaf;
                iterCounter = node->leaves_in_subtree;
                return;
            }

            // Look for an edge
            auto nextNode = n->getEdge(search[0]);
            if (!nextNode) {
                node = nullptr;
                iterLeafNode = nullptr;
                iterCounter = 0;
                return;
            }

            // Consume the search prefix
            if (hasPrefix(search, nextNode->prefix)) {
                search = K(search.begin() + nextNode->prefix.size(), search.end());
            } else if (hasPrefix(nextNode->prefix, search)) {
                node = nextNode;
                iterLeafNode = node->minLeaf;
                iterCounter = node->leaves_in_subtree;
                return;
            } else {
                node = nullptr;
                iterLeafNode = nullptr;
                iterCounter = 0;
                return;
            }

            n = nextNode;
        }
    }

    // Seeks the iterator to a given prefix
    void seekPrefix(const K& prefix) {
        seekPrefixWatch(prefix);
    }

    // Seeks the iterator to a given key
    void seekLowerBound(const K& key) {
        seekLowerBoundFlag = true;
        seekPrefixWatch(key);
    }

    // Returns the next element in order
    IteratorResult<K, T> next() {
        IteratorResult<K, T> result;
        result.found = false;

        if (iterCounter > 0 && iterLeafNode) {
            iterCounter--;

            result.key = iterLeafNode->key;
            result.val = iterLeafNode->val;
            result.found = true;
            iterLeafNode = iterLeafNode->nextLeaf;
            return result;
        }

        return result;
    }
};

// Helper functions to create iterators
template<typename K, typename T>
Iterator<K, T> createIterator(std::shared_ptr<Node<K, T>> node) {
    return Iterator<K, T>(node);
}

template<typename K, typename T>
ReverseIterator<K, T> createReverseIterator(std::shared_ptr<Node<K, T>> node) {
    return ReverseIterator<K, T>(node);
}

template<typename K, typename T>
PrefixIterator<K, T> createPrefixIterator(std::shared_ptr<Node<K, T>> node, const K& path) {
    return PrefixIterator<K, T>(node, path);
}

#endif // ITERATOR_H