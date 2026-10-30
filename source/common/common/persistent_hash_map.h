#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "absl/hash/hash.h"

namespace Envoy {

/**
 * A persistent hash map with structural sharing. Copying a map is O(1) and each copy is an
 * independent snapshot that never observes later updates to the original. An update copies only
 * the nodes on the path to the affected entry that a snapshot still shares and modifies the rest
 * in place, so it costs O(log N) rather than the O(N) copy a flat map needs to publish a new read
 * only version.
 *
 * Nodes are reference counted and only modified in place while this map holds the sole reference
 * to them, so a snapshot may be read on other threads while the owner keeps updating its own copy.
 *
 * Lookups are heterogeneous whenever Hash and Eq accept the lookup key type. For example
 * PersistentHashMap<std::string, V, absl::Hash<absl::string_view>, std::equal_to<>> can be searched
 * with an absl::string_view without allocating a std::string.
 */
template <class Key, class Value, class Hash = absl::Hash<Key>, class Eq = std::equal_to<Key>>
class PersistentHashMap {
public:
  PersistentHashMap() = default;
  PersistentHashMap(const PersistentHashMap&) = default;
  PersistentHashMap& operator=(const PersistentHashMap&) = default;
  PersistentHashMap(PersistentHashMap&& other) noexcept
      : root_(std::move(other.root_)), size_(std::exchange(other.size_, 0)) {}
  PersistentHashMap& operator=(PersistentHashMap&& other) noexcept {
    root_ = std::move(other.root_);
    size_ = std::exchange(other.size_, 0);
    return *this;
  }

  /**
   * @param key the key to look up.
   * @return const Value* the value stored for the key, or nullptr if absent. The pointer stays
   *         valid until this map is modified or destroyed.
   */
  template <class K> const Value* find(const K& key) const {
    const Entry* entry = findEntry(Hash{}(key), key);
    return entry != nullptr ? &entry->value : nullptr;
  }

  /**
   * Inserts the entry if the key is absent.
   * @param key the key to insert.
   * @param value the value to store.
   * @return bool true if the entry was inserted, false if the key was already present.
   */
  bool insert(Key key, Value value) {
    const uint64_t hash = Hash{}(key);
    // Probe before mutating so inserting a key that is already present does not copy the shared
    // path down to it.
    if (findEntry(hash, key) != nullptr) {
      return false;
    }
    if (root_.get() == nullptr) {
      root_ = NodePtr(new Node());
    }
    insertInNode(root_, hash, 0, Entry{std::move(key), std::move(value)});
    ++size_;
    return true;
  }

  /**
   * Removes the entry for the key.
   * @param key the key to remove.
   * @return bool true if an entry was removed.
   */
  template <class K> bool erase(const K& key) {
    const uint64_t hash = Hash{}(key);
    // Probe before mutating so erasing an absent key does not copy the shared path.
    if (findEntry(hash, key) == nullptr) {
      return false;
    }
    eraseFromNode(root_, hash, 0, key);
    if (--size_ == 0) {
      root_ = NodePtr();
    }
    return true;
  }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  /**
   * Invokes cb(const Key&, const Value&) for every entry. Iteration order is unspecified.
   * @param cb the callback to invoke for each entry.
   */
  template <class Callback> void forEach(Callback&& cb) const {
    if (root_.get() != nullptr) {
      forEachInNode(*root_, cb);
    }
  }

private:
  static constexpr uint32_t BitsPerLevel = 5;
  // Depth at which the hash bits run out. A node at this depth is a list of equal hash entries.
  static constexpr uint32_t MaxDepth = (64 + BitsPerLevel - 1) / BitsPerLevel;

  struct Entry {
    Key key;
    Value value;
  };

  struct Node;

  // Reference counted pointer to a node shared between map versions.
  class NodePtr {
  public:
    NodePtr() = default;
    explicit NodePtr(Node* node) : node_(node) {}
    NodePtr(const NodePtr& other) : node_(other.node_) {
      if (node_ != nullptr) {
        node_->refs.fetch_add(1, std::memory_order_relaxed);
      }
    }
    NodePtr(NodePtr&& other) noexcept : node_(std::exchange(other.node_, nullptr)) {}
    NodePtr& operator=(NodePtr other) noexcept {
      std::swap(node_, other.node_);
      return *this;
    }
    ~NodePtr() {
      if (node_ != nullptr && node_->refs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete node_;
      }
    }

    Node* get() const { return node_; }
    Node& operator*() const { return *node_; }
    Node* operator->() const { return node_; }

    // The acquire pairs with the release of the other references, so that reads of the node made
    // through them happen before the node is modified in place.
    bool unique() const { return node_->refs.load(std::memory_order_acquire) == 1; }

  private:
    Node* node_{nullptr};
  };

  // Bitmap indexed node. Bit i of data_map marks that the slot for hash fragment i holds an entry
  // and bit i of node_map that it holds a child node. Entries and children are stored densely in
  // fragment order, so the position of slot i is the number of lower bits set in its bitmap.
  struct Node {
    Node() = default;
    Node(const Node& other)
        : data_map(other.data_map), node_map(other.node_map), entries(other.entries),
          children(other.children) {}

    std::atomic<uint32_t> refs{1};
    uint32_t data_map{0};
    uint32_t node_map{0};
    std::vector<Entry> entries;
    std::vector<NodePtr> children;
  };

  static uint32_t fragmentBit(uint64_t hash, uint32_t depth) {
    return 1u << ((hash >> (depth * BitsPerLevel)) & ((1u << BitsPerLevel) - 1));
  }

  static size_t index(uint32_t bitmap, uint32_t bit) {
    return static_cast<size_t>(std::popcount(bitmap & (bit - 1)));
  }

  // Returns the node for in place modification. The node is replaced by a copy first unless this
  // map holds the only reference to it, so that snapshots sharing it are left intact.
  static Node& mutableNode(NodePtr& node) {
    if (!node.unique()) {
      node = NodePtr(new Node(*node));
    }
    return *node;
  }

  template <class K> const Entry* findEntry(uint64_t hash, const K& key) const {
    const Node* node = root_.get();
    if (node == nullptr) {
      return nullptr;
    }
    for (uint32_t depth = 0; depth < MaxDepth; ++depth) {
      const uint32_t bit = fragmentBit(hash, depth);
      if (node->data_map & bit) {
        const Entry& entry = node->entries[index(node->data_map, bit)];
        return Eq{}(entry.key, key) ? &entry : nullptr;
      }
      if (!(node->node_map & bit)) {
        return nullptr;
      }
      node = node->children[index(node->node_map, bit)].get();
    }
    // The hash bits are exhausted, so the node holds a list of entries with equal hashes.
    for (const Entry& entry : node->entries) {
      if (Eq{}(entry.key, key)) {
        return &entry;
      }
    }
    return nullptr;
  }

  // Inserts the entry below the node. insert() verifies the key is absent with findEntry before
  // mutating, so the path this copies is always modified rather than copied for a no-op.
  static void insertInNode(NodePtr& node_ptr, uint64_t hash, uint32_t depth, Entry&& entry) {
    Node& node = mutableNode(node_ptr);
    if (depth == MaxDepth) {
      node.entries.push_back(std::move(entry));
      return;
    }
    const uint32_t bit = fragmentBit(hash, depth);
    if (node.data_map & bit) {
      const size_t slot = index(node.data_map, bit);
      Entry& existing = node.entries[slot];
      // Two different keys share this fragment, so push both down into a new child node.
      const uint64_t existing_hash = Hash{}(existing.key);
      NodePtr child =
          mergeEntries(std::move(existing), existing_hash, std::move(entry), hash, depth + 1);
      node.entries.erase(node.entries.begin() + slot);
      node.data_map &= ~bit;
      node.node_map |= bit;
      node.children.insert(node.children.begin() + index(node.node_map, bit), std::move(child));
      return;
    }
    if (node.node_map & bit) {
      insertInNode(node.children[index(node.node_map, bit)], hash, depth + 1, std::move(entry));
      return;
    }
    node.data_map |= bit;
    node.entries.insert(node.entries.begin() + index(node.data_map, bit), std::move(entry));
  }

  // Returns a new node holding two entries whose hashes agree on every fragment above this depth.
  static NodePtr mergeEntries(Entry&& a, uint64_t hash_a, Entry&& b, uint64_t hash_b,
                              uint32_t depth) {
    NodePtr node(new Node());
    if (depth == MaxDepth) {
      node->entries.push_back(std::move(a));
      node->entries.push_back(std::move(b));
      return node;
    }
    const uint32_t bit_a = fragmentBit(hash_a, depth);
    const uint32_t bit_b = fragmentBit(hash_b, depth);
    if (bit_a == bit_b) {
      node->node_map = bit_a;
      node->children.push_back(mergeEntries(std::move(a), hash_a, std::move(b), hash_b, depth + 1));
      return node;
    }
    node->data_map = bit_a | bit_b;
    if (bit_a < bit_b) {
      node->entries.push_back(std::move(a));
      node->entries.push_back(std::move(b));
    } else {
      node->entries.push_back(std::move(b));
      node->entries.push_back(std::move(a));
    }
    return node;
  }

  // Removes the key from below the node. erase() verifies the key is present with findEntry before
  // mutating, so the path this copies is always modified rather than copied for a no-op. A child
  // left with a single entry is pulled up into its parent so the tree stays compact.
  template <class K>
  static void eraseFromNode(NodePtr& node_ptr, uint64_t hash, uint32_t depth, const K& key) {
    Node& node = mutableNode(node_ptr);
    if (depth == MaxDepth) {
      const auto it = std::find_if(node.entries.begin(), node.entries.end(),
                                   [&key](const Entry& entry) { return Eq{}(entry.key, key); });
      node.entries.erase(it);
      return;
    }
    const uint32_t bit = fragmentBit(hash, depth);
    if (node.data_map & bit) {
      const size_t slot = index(node.data_map, bit);
      node.entries.erase(node.entries.begin() + slot);
      node.data_map &= ~bit;
      return;
    }
    const size_t slot = index(node.node_map, bit);
    NodePtr& child = node.children[slot];
    eraseFromNode(child, hash, depth + 1, key);
    if (child->node_map == 0 && child->entries.size() == 1) {
      Entry entry = std::move(child->entries.front());
      node.children.erase(node.children.begin() + slot);
      node.node_map &= ~bit;
      node.data_map |= bit;
      node.entries.insert(node.entries.begin() + index(node.data_map, bit), std::move(entry));
    }
  }

  template <class Callback> static void forEachInNode(const Node& node, Callback& cb) {
    for (const Entry& entry : node.entries) {
      cb(entry.key, entry.value);
    }
    for (const NodePtr& child : node.children) {
      forEachInNode(*child, cb);
    }
  }

  NodePtr root_;
  size_t size_{0};
};

} // namespace Envoy
