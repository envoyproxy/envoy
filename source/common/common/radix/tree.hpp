//
// Created by Ashesh Vidyut on 22/03/25.
//

#ifndef TREE_H
#define TREE_H

#include "node.hpp"
#include "iterator.cpp"
#include <memory>
#include <vector>
#include <functional>
#include <optional>
#include <tuple>
#include <stack>

template<typename K>
K concat(const K& a, const K& b);  // Implementation in node.hpp

template<typename K, typename T>
class Node;

template<typename K, typename T>
class LeafNode;

// Forward declaration for PrefixIterator
template<typename K, typename T>
class PrefixIterator;

// Simple object pool for reducing allocations
template<typename T>
class ObjectPool {
private:
    std::stack<std::unique_ptr<T>> pool;
    std::function<std::unique_ptr<T>()> factory;

public:
    ObjectPool(std::function<std::unique_ptr<T>()> f) : factory(f) {}

    std::unique_ptr<T> acquire() {
        if (pool.empty()) {
            return factory();
        }
        auto obj = std::move(pool.top());
        pool.pop();
        return obj;
    }

    void release(std::unique_ptr<T> obj) {
        pool.push(std::move(obj));
    }
};

// Result structure for delete operations
template<typename K, typename T>
struct DeleteResult {
    std::shared_ptr<Node<K, T>> node;
    std::shared_ptr<LeafNode<K, T>> leaf;
};

// Result structure for delete prefix operations
template<typename K, typename T>
struct DeletePrefixResult {
    std::shared_ptr<Node<K, T>> node;
    int numDeletions;
};

// Tree class
template<typename K, typename T>
class Tree {
private:
    std::shared_ptr<Node<K, T>> root;
    int size;

    // Object pools for reducing allocations
    static ObjectPool<Node<K, T>> nodePool;
    static ObjectPool<LeafNode<K, T>> leafPool;

    friend class Transaction;

public:
    Tree() : root(std::make_shared<Node<K, T>>()), size(0) {}

    std::shared_ptr<Node<K, T>> getRoot() const {
        return root;
    }

    int len() const {
        return size;
    }

    int GetLeavesInSubtree() const {
        return root->leaves_in_subtree;
    }

    // Transaction class for atomic operations
    class Transaction {
    protected:
        std::shared_ptr<Node<K, T>> root;
        int size;
        Tree<K, T>& tree;

        friend class Tree;

    public:
        Transaction(Tree<K, T>& t) : root(t.root), size(t.size), tree(t) {}

        std::tuple<std::shared_ptr<Node<K, T>>, std::optional<T>, bool> insert(
            std::shared_ptr<Node<K, T>> n,
            const K& k,
            const K& search,
            const T& v) {
            std::optional<T> oldVal;
            bool didUpdate = false;

            // Handle key exhaustion
            if (search.empty()) {
                if (n->leaf) {
                    oldVal = n->leaf->val;
                    didUpdate = true;
                }
                n->leaf = std::make_shared<LeafNode<K, T>>(k, v);
                n->computeLinks();
                return {n, oldVal, didUpdate};
            }

            // Look for the edge
            int idx;
            auto child = n->getEdge(search[0], &idx);

            // No edge, create one
            if (!child) {
                auto leaf = std::make_shared<LeafNode<K, T>>(k, v);
                auto newNode = std::make_shared<Node<K, T>>();
                newNode->leaf = leaf;
                newNode->minLeaf = leaf.get();
                newNode->maxLeaf = leaf.get();
                newNode->prefix = search;
                newNode->leaves_in_subtree = 1;

                Edge<K, T> e;
                e.label = search[0];
                e.node = newNode;
                n->addEdge(e);
                n->computeLinks();
                return {n, std::nullopt, false};
            }

            // Determine longest prefix of the search key on match
            size_t commonPrefix = longestPrefix(search, child->prefix);
            if (commonPrefix == child->prefix.size()) {
                K newSearch(search.begin() + commonPrefix, search.end());
                auto [newChild, oldVal, didUpdate] = insert(child, k, newSearch, v);
                if (newChild) {
                    n->edges[idx].node = newChild;
                    n->computeLinks();
                    return {n, oldVal, didUpdate};
                }
                return {nullptr, oldVal, didUpdate};
            }

            // Optimized split: minimize allocations
            // Instead of creating a new split node, we can sometimes modify the existing child
            if (commonPrefix == 0) {
                // No common prefix, just add as a new edge
                auto leaf = std::make_shared<LeafNode<K, T>>(k, v);
                auto newNode = std::make_shared<Node<K, T>>();
                newNode->leaf = leaf;
                newNode->minLeaf = leaf.get();
                newNode->maxLeaf = leaf.get();
                newNode->prefix = search;
                newNode->leaves_in_subtree = 1;

                Edge<K, T> e;
                e.label = search[0];
                e.node = newNode;
                n->addEdge(e);
                n->computeLinks();
                return {n, std::nullopt, false};
            }

            // We need to split - create minimal new structure
            auto splitNode = std::make_shared<Node<K, T>>();
            splitNode->prefix = K(search.begin(), search.begin() + commonPrefix);

            // Move existing child under split node
            Edge<K, T> childEdge;
            childEdge.label = child->prefix[commonPrefix];
            childEdge.node = child;
            splitNode->addEdge(childEdge);
            child->prefix = K(child->prefix.begin() + commonPrefix, child->prefix.end());

            // Handle the new key
            K remainingSearch(search.begin() + commonPrefix, search.end());
            if (remainingSearch.empty()) {
                // New key ends at split node
                auto leaf = std::make_shared<LeafNode<K, T>>(k, v);
                splitNode->leaf = leaf;
                splitNode->minLeaf = leaf.get();
                splitNode->maxLeaf = leaf.get();
                splitNode->leaves_in_subtree++;
            } else {
                // New key continues
                auto leaf = std::make_shared<LeafNode<K, T>>(k, v);
                auto newNode = std::make_shared<Node<K, T>>();
                newNode->leaf = leaf;
                newNode->minLeaf = leaf.get();
                newNode->maxLeaf = leaf.get();
                newNode->prefix = remainingSearch;
                newNode->leaves_in_subtree = 1;

                Edge<K, T> newEdge;
                newEdge.label = remainingSearch[0];
                newEdge.node = newNode;
                splitNode->addEdge(newEdge);
            }

            // Update parent
            Edge<K, T> splitEdge;
            splitEdge.label = search[0];
            splitEdge.node = splitNode;
            n->replaceEdge(splitEdge);
            
            splitNode->computeLinks();
            n->computeLinks();
            return {n, std::nullopt, false};
        }

        DeleteResult<K, T> del(std::shared_ptr<Node<K, T>> parent, std::shared_ptr<Node<K, T>> n, const K& search) {
            (void)parent; // Suppress unused parameter warning
            DeleteResult<K, T> result;
            result.node = nullptr;
            result.leaf = nullptr;

            // If we're at the end of the search, we're deleting
            if (search.empty()) {
                if (n->leaf) {
                    // Delete the leaf
                    result.leaf = n->leaf;
                    n->leaf = nullptr;
                    n->minLeaf = nullptr;
                    n->maxLeaf = nullptr;
                    size--;

                    // If the node has no edges, it can be removed
                    if (n->edges.empty()) {
                        return result;
                    }

                    // If the node has only one edge, merge with the child
                    if (n->edges.size() == 1) {
                        mergeChild(n);
                        result.node = n;
                        return result;
                    }

                    // Otherwise, just update the node
                    n->computeLinks();
                    result.node = n;
                    return result;
                }
                return result;
            }

            // Look for an edge
            if (search.empty()) {
                result.node = n;
                return result;
            }
            int idx;
            auto child = n->getEdge(search[0], &idx);
            if (child) {
                // Consume the search prefix
                K newSearch = search;
                if (!child->prefix.empty() && !newSearch.empty()) {
                    if (child->prefix.size() < newSearch.size()) {
                        K searchPrefix(newSearch.begin(), newSearch.begin() + child->prefix.size());
                        if (child->prefix != searchPrefix) {
                            result.node = n;
                            return result;
                        }
                        newSearch = K(newSearch.begin() + child->prefix.size(), newSearch.end());
                    } else {
                        if (child->prefix != newSearch) {
                            result.node = n;
                            return result;
                        }
                        newSearch.clear();
                    }
                }

                // Recursively delete
                auto delResult = del(n, child, newSearch);
                if (delResult.node) {
                    Edge<K, T> e = n->edges[idx];
                    e.node = delResult.node;
                    n->replaceEdge(e);
                } else {
                    n->edges.erase(n->edges.begin() + idx);
                    if (n->edges.empty() && !n->leaf) {
                        return result;
                    }
                    // Check if we should merge after edge deletion
                    if (n->edges.size() == 1 && !n->leaf) {
                        mergeChild(n);
                    }
                }

                // Update min/max leaves
                n->computeLinks();
                result.node = n;
                result.leaf = delResult.leaf;
                return result;
            }

            result.node = n;
            return result;
        }

        DeletePrefixResult<K, T> deletePrefix(std::shared_ptr<Node<K, T>> n, const K& search) {
            DeletePrefixResult<K, T> result;
            result.node = nullptr;
            result.numDeletions = 0;

            // Handle key exhaustion
            if (search.empty()) {
                // Delete all leaves under this node
                int count = 0;
                std::function<void(std::shared_ptr<Node<K, T>>)> countLeaves = [&count, &countLeaves](std::shared_ptr<Node<K, T>> node) {
                    if (node->leaf) {
                        count++;
                    }
                    for (const auto& edge : node->edges) {
                        countLeaves(edge.node);
                    }
                };
                countLeaves(n);
                size -= count;
                result.numDeletions = count;
                return result;
            }

            // Look for an edge
            int idx;
            auto child = n->getEdge(search[0], &idx);
            if (child) {
                // Consume the search prefix
                K newSearch = search;
                if (!child->prefix.empty() && !newSearch.empty()) {
                    if (child->prefix.size() < newSearch.size()) {
                        K searchPrefix(newSearch.begin(), newSearch.begin() + child->prefix.size());
                        if (child->prefix != searchPrefix) {
                            result.node = n;
                            return result;
                        }
                        newSearch = K(newSearch.begin() + child->prefix.size(), newSearch.end());
                    } else {
                        if (child->prefix != newSearch) {
                            result.node = n;
                            return result;
                        }
                        newSearch.clear();
                    }
                }

                // Recursively delete
                auto delResult = deletePrefix(child, newSearch);
                if (delResult.node) {
                    Edge<K, T> e = n->edges[idx];
                    e.node = delResult.node;
                    n->replaceEdge(e);
                } else {
                    n->edges.erase(n->edges.begin() + idx);
                    if (n->edges.empty() && !n->leaf) {
                        return result;
                    }
                    // Check if we should merge after edge deletion
                    if (n->edges.size() == 1 && !n->leaf) {
                        mergeChild(n);
                    }
                }

                // Update min/max leaves
                n->computeLinks();
                result.node = n;
                result.numDeletions = delResult.numDeletions;
                return result;
            }

            result.node = n;
            return result;
        }

        void mergeChild(std::shared_ptr<Node<K, T>> n) {
            if (n->edges.size() != 1) {
                return;
            }

            auto child = n->edges[0].node;
            
            // Merge the nodes by copying child's properties to parent
            n->prefix = concat(n->prefix, child->prefix);
            n->leaf = child->leaf;
            n->minLeaf = child->minLeaf;
            n->maxLeaf = child->maxLeaf;
            n->leaves_in_subtree = child->leaves_in_subtree;
            
            if (!child->edges.empty()) {
                n->edges = child->edges;
            } else {
                n->edges.clear();
            }
        }

        int trackChannelsAndCount(std::shared_ptr<Node<K, T>> n) {
            int count = 0;
            std::function<void(std::shared_ptr<Node<K, T>>)> countLeaves = [&count, &countLeaves](std::shared_ptr<Node<K, T>> node) {
                if (node->leaf) {
                    count++;
                }
                for (const auto& edge : node->edges) {
                    countLeaves(edge.node);
                }
            };
            countLeaves(n);
            return count;
        }

        Transaction clone() {
            return Transaction(tree);
        }

        Tree<K, T> commit() {
            tree.root = root;
            tree.size = size;
            return tree;
        }

        Tree<K, T> commitOnly() {
            return tree;
        }
    };

    Transaction txn() {
        return Transaction(*this);
    }

    std::tuple<Tree<K, T>, std::optional<T>, bool> insert(const K& k, const T& v) {
        auto txn = this->txn();
        auto [newRoot, oldVal, didUpdate] = txn.insert(root, k, k, v);
        root = newRoot;
        if (!didUpdate) {
            size++;
        }
        return {*this, oldVal, didUpdate};
    }

    std::tuple<Tree<K, T>, std::optional<T>, bool> del(const K& k) {
        auto txn = this->txn();
        auto result = txn.del(nullptr, root, k);
        if (result.node) {
            root = result.node;
        }
        return {*this, result.leaf ? std::optional<T>(result.leaf->val) : std::nullopt, result.leaf != nullptr};
    }

    std::tuple<Tree<K, T>, bool, int> deletePrefix(const K& k) {
        auto txn = this->txn();
        auto result = txn.deletePrefix(root, k);
        if (result.node) {
            root = result.node;
        }
        return {*this, result.numDeletions > 0, result.numDeletions};
    }

    std::optional<T> Get(const K& search) const {
        T result;
        if (root->Get(search, result)) {
            return result;
        }
        return std::nullopt;
    }

    LongestPrefixResult<K, T> LongestPrefix(const K& search) const {
        return root->LongestPrefix(search);
    }

    // GetAtIndex is used to lookup a specific key, returning
    // the value and if it was found
    std::tuple<K, T, bool> GetAtIndex(int index) const {
        return root->GetAtIndex(index);
    }

    Iterator<K, T> iterator() const {
        return Iterator<K, T>(root);
    }

    // Range-based for loop support
    Iterator<K, T> begin() const {
        return Iterator<K, T>(root);
    }

    Iterator<K, T> end() const {
        return Iterator<K, T>(nullptr);
    }

    // PrefixIterator returns an iterator that walks down the tree following a path
    PrefixIterator<K, T> prefixIterator(const K& key) const {
        return PrefixIterator<K, T>(root, key);
    }

    // findMatchingPrefixes finds all keys that are prefixes of the given key
    // using PrefixIterator to walk down the tree following the search key path
    std::vector<std::pair<K, T>> findMatchingPrefixes(const K& searchKey) const {
        std::vector<std::pair<K, T>> results;

        if (searchKey.empty()) {
            return results;
        }

        // Use PrefixIterator to walk down the tree following the search key path
        auto prefixIter = prefixIterator(searchKey);

        // Collect all leaf nodes along the path
        while (true) {
            auto result = prefixIter.next();
            if (!result.found) {
                break;
            }

            // Check if this key is a prefix of the search key
            if (hasPrefix(searchKey, result.key)) {
                results.push_back({result.key, result.val});
            }
        }
        
        return results;
    }
};

// Non-template function declaration
void initializeTree();

#endif // TREE_H
