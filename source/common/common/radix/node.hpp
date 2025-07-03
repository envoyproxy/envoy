//
// Created by Ashesh Vidyut on 22/03/25.
//

#ifndef NODE_H
#define NODE_H

#include <regex.h>  // if you use regex_t for patterns
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>
#include <optional>
#include <tuple>

// Forward declarations
template<typename K, typename T>
class Node;

template<typename K, typename T>
class LeafNode;

// Edge structure
template<typename K, typename T>
struct Edge {
    typename K::value_type label;
    std::shared_ptr<Node<K, T>> node;
};

// Result structure for LongestPrefix
template<typename K, typename T>
struct LongestPrefixResult {
    K key;
    T val;
    bool found;
};

// LeafNode definition
template<typename K, typename T>
class LeafNode {
public:
    K key;                  // key data
    T val;                  // value
    LeafNode<K, T>* nextLeaf;  // Use raw pointers for internal links
    LeafNode<K, T>* prevLeaf;  // Use raw pointers for internal links

    LeafNode(const K& k, const T& v);
};

// Node structure
template<typename K, typename T>
class Node {
public:
    std::shared_ptr<LeafNode<K, T>> leaf;
    LeafNode<K, T>* minLeaf;  // Use raw pointers for internal links
    LeafNode<K, T>* maxLeaf;  // Use raw pointers for internal links
    K prefix;
    std::vector<Edge<K, T>> edges;
    int leaves_in_subtree;

    Node();

    // Returns the edge with the given label, or nullptr if not found
    std::shared_ptr<Node<K, T>> getEdge(typename K::value_type label, int* out_index = nullptr) const;

    // Replaces an edge with the given label
    void replaceEdge(const Edge<K, T>& e);

    // Deletes an edge with the given label
    void delEdge(typename K::value_type label);

    // Adds an edge in sorted order
    void addEdge(const Edge<K, T>& e);

    // Returns the lower bound edge for the given label
    std::shared_ptr<Node<K, T>> getLowerBoundEdge(typename K::value_type label, int* out_index) const;

    // Returns the minimum leaf in the node
    LeafNode<K, T>* minimumLeaf(bool* found = nullptr) const;

    // Returns the maximum leaf in the node
    LeafNode<K, T>* maximumLeaf(bool* found = nullptr) const;

    // Updates the node's minLeaf and maxLeaf fields
    void updateMinMaxLeaves();

    // Computes leaf links and updates leaves_in_subtree count
    void computeLinks();

    // Checks if the node is a leaf
    bool isLeaf() const;

    // Gets a value from the node given a key
    bool Get(const K& search, T& result) const;

    // LongestPrefix is like Get, but instead of an exact match,
    // it will return the longest prefix match.
    LongestPrefixResult<K, T> LongestPrefix(const K& search) const;

    // GetAtIndex returns the key and value at the specified index
    std::tuple<K, T, bool> GetAtIndex(int index) const;

    // SearchIndex searches for the key and value at the specified index
    std::tuple<K, T, bool> SearchIndex(int idx) const;

    // getNextIndexEdge finds the next edge that contains the index
    std::tuple<int, std::shared_ptr<Node<K, T>>> getNextIndexEdge(int idx) const;
};

// Helper function declarations
template<typename K>
bool hasPrefix(const K& str, const K& prefix);

template<typename K>
int longestPrefix(const K& k1, const K& k2);

template<typename K>
K concat(const K& a, const K& b);

// Non-template function declarations
void initializeNode();

// Explicit template instantiation declarations
extern template class Node<std::string, std::string>;
extern template class Node<std::string, int>;
extern template class Node<std::string, double>;
extern template class Node<std::vector<uint8_t>, std::string>;
extern template class LeafNode<std::string, std::string>;
extern template class LeafNode<std::string, int>;
extern template class LeafNode<std::string, double>;
extern template class LeafNode<std::vector<uint8_t>, std::string>;

#endif // NODE_H
