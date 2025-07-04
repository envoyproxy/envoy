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

// LowerBoundIterator class for finding the smallest key >= given key
template<typename K, typename T>
class LowerBoundIterator {
private:
    std::shared_ptr<Node<K, T>> node;
    LeafNode<K, T>* iterLeafNode;
    int iterCounter;
    K key;
    std::vector<std::shared_ptr<Node<K, T>>> stack;
    std::vector<int> stackIndices;

public:
    LowerBoundIterator(std::shared_ptr<Node<K, T>> n) : node(n) {
        if (node) {
            iterLeafNode = node->minLeaf;
            iterCounter = node->leaves_in_subtree;
        } else {
            iterLeafNode = nullptr;
            iterCounter = 0;
        }
    }

    // Seeks the iterator to the smallest key that is greater or equal to the given key
    void seekLowerBound(const K& searchKey) {
        // Wipe the stack
        stack.clear();
        stackIndices.clear();
        
        // Reset iterLeafNode to nullptr initially
        iterLeafNode = nullptr;
        
        // i.node starts off in the common case as pointing to the root node of the
        // tree. By the time we return we have either found a lower bound and setup
        // the stack to traverse all larger keys, or we have not and the stack and
        // node should both be nil to prevent the iterator from assuming it is just
        // iterating the whole tree from the root node. Either way this needs to end
        // up as nil so just set it here.
        auto n = node;
        node = nullptr;
        K search = searchKey;

        auto found = [this](std::shared_ptr<Node<K, T>> foundNode) {
            // If the found node has a leaf, set it as the initial iterLeafNode
            if (foundNode->leaf) {
                iterLeafNode = foundNode->leaf.get();
            } else {
                // If no leaf, find the minimum leaf in this subtree
                auto minLeaf = findMinLeaf(foundNode);
                if (minLeaf) {
                    iterLeafNode = minLeaf;
                }
            }
            stack.push_back(foundNode);
        };

        auto findMin = [this, &found](std::shared_ptr<Node<K, T>> n) {
            auto minNode = recurseMin(n);
            if (minNode) {
                found(minNode);
                return;
            }
        };

        while (true) {
            // Compare current prefix with the search key's same-length prefix.
            int prefixCmp;
            if (n->prefix.size() < search.size()) {
                K searchPrefix(search.begin(), search.begin() + n->prefix.size());
                prefixCmp = n->prefix.compare(searchPrefix);
            } else {
                K nodePrefix(n->prefix.begin(), n->prefix.begin() + search.size());
                prefixCmp = nodePrefix.compare(search);
            }

            if (prefixCmp > 0) {
                // Prefix is larger, that means the lower bound is greater than the search
                // and from now on we need to follow the minimum path to the smallest
                // leaf under this subtree.
                findMin(n);
                return;
            }

            if (prefixCmp < 0) {
                // Prefix is smaller than search prefix, that means there is no lower
                // bound
                node = nullptr;
                iterLeafNode = nullptr;
                return;
            }

            // Prefix is equal, we are still heading for an exact match. If this is a
            // leaf and an exact match we're done.
            if (n->leaf && n->leaf->key == searchKey) {
                found(n);
                return;
            }

            // Consume the search prefix if the current node has one. Note that this is
            // safe because if n.prefix is longer than the search slice prefixCmp would
            // have been > 0 above and the method would have already returned.
            if (n->prefix.size() <= search.size()) {
                search = K(search.begin() + n->prefix.size(), search.end());
            }

            if (search.empty()) {
                // We've exhausted the search key, but the current node is not an exact
                // match or not a leaf. That means that the leaf value if it exists, and
                // all child nodes must be strictly greater, the smallest key in this
                // subtree must be the lower bound.
                findMin(n);
                return;
            }

            // Otherwise, take the lower bound next edge.
            int idx;
            auto lbNode = n->getLowerBoundEdge(search[0], &idx);
            if (!lbNode) {
                iterLeafNode = nullptr;
                return;
            }

            // Create stack edges for the all strictly higher edges in this node.
            for (size_t i = idx + 1; i < n->edges.size(); i++) {
                stack.push_back(n->edges[i].node);
                stackIndices.push_back(0);
            }

            // Recurse
            n = lbNode;
        }
    }

    // Returns the next node in order
    IteratorResult<K, T> next() {
        IteratorResult<K, T> result;
        result.found = false;

        // If we have a current leaf node, return it and move to next
        if (iterLeafNode) {
            result.key = iterLeafNode->key;
            result.val = iterLeafNode->val;
            result.found = true;
            iterLeafNode = iterLeafNode->nextLeaf;
            return result;
        }

        // If we have nodes in the stack, process them
        while (!stack.empty()) {
            auto currentNode = stack.back();
            stack.pop_back();
            
            if (currentNode->leaf) {
                result.key = currentNode->leaf->key;
                result.val = currentNode->leaf->val;
                result.found = true;
                iterLeafNode = currentNode->leaf->nextLeaf;
                return result;
            }

            // Add all children to stack in reverse order (so we process them in order)
            for (int i = currentNode->edges.size() - 1; i >= 0; i--) {
                stack.push_back(currentNode->edges[i].node);
            }
        }

        return result;
    }

    // Get the current iterLeafNode for external access
    LeafNode<K, T>* getIterLeafNode() const {
        return iterLeafNode;
    }

private:
    // Helper method to find the minimum leaf in a subtree
    std::shared_ptr<Node<K, T>> recurseMin(std::shared_ptr<Node<K, T>> n) {
        if (!n) return nullptr;
        
        if (n->leaf) {
            return n;
        }
        
        if (n->edges.empty()) {
            return nullptr;
        }
        
        return recurseMin(n->edges[0].node);
    }
    
    // Helper method to find the minimum leaf node in a subtree
    LeafNode<K, T>* findMinLeaf(std::shared_ptr<Node<K, T>> n) {
        if (!n) return nullptr;
        return n->minLeaf;
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

    // Seeks the iterator to the smallest key that is greater or equal to the given key
    void seekLowerBound(const K& key) {
        // This method is kept for compatibility but the actual implementation
        // would need to be updated to work with the leaf-based approach
        // For now, we'll use the prefix seek as a fallback
        seekPrefix(key);
    }

    // Returns the next node in order
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

    friend class ReverseIterator<K, T>;
};

// Helper function to create an iterator from a node
template<typename K, typename T>
Iterator<K, T> createIterator(std::shared_ptr<Node<K, T>> node) {
    return Iterator<K, T>(node);
}

// Helper function to create a reverse iterator from a node
template<typename K, typename T>
ReverseIterator<K, T> createReverseIterator(std::shared_ptr<Node<K, T>> node) {
    return ReverseIterator<K, T>(node);
}

// Helper function to create a lower bound iterator from a node
template<typename K, typename T>
LowerBoundIterator<K, T> createLowerBoundIterator(std::shared_ptr<Node<K, T>> node) {
    return LowerBoundIterator<K, T>(node);
}

#endif // ITERATOR_H