#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace Envoy {

/**
 * This is a specialized structure intended for static header maps, but
 * there may be other use cases.
 * The structure is:
 * 1. a length-based lookup table so only keys the same length as the
 * target key are considered.
 * 2. a trie that branches on the "most divisions" position of the key.
 *
 * For example, if we consider the case where the set of headers is
 * `x-prefix-banana`
 * `x-prefix-babana`
 * `x-prefix-apple`
 * `x-prefix-pineapple`
 * `x-prefix-barana`
 * `x-prefix-banaka`
 *
 * A standard front-first trie looking for `x-prefix-banana` would walk
 * 7 nodes through the tree, first for `x`, then for `-`, etc.
 *
 * This structure first jumps to matching length, eliminating in this
 * example case apple and pineapple.
 * Then the "best split" node is on
 *   `x-prefix-banana`
 *               ^
 * so the first node has 3 non-miss branches, n, b and r for that position.
 * Down that n branch, the "best split" is on
 *   `x-prefix-banana`
 *                 ^
 * which has two branches, n or k.
 * Down the n branch is the leaf node (only `x-prefix-banana` remains) - at
 * this point a regular string-compare checks if the key is an exact match
 * for the string node.
 */
template <class Value> class CompiledStringMap {
  using FindFn = std::function<Value(const absl::string_view&)>;

public:
  using KV = std::pair<std::string, Value>;
  /**
   * Returns the value with a matching key, or the default value
   * (typically nullptr) if the key was not present.
   * @param key the key to look up.
   */
  Value find(const absl::string_view& key) const {
    if (key.size() >= table_.size() || table_[key.size()] == nullptr) {
      return {};
    }
    return table_[key.size()](key);
  };
  /**
   * Construct the lookup table. This is a somewhat slow multi-pass
   * operation - using this structure is not recommended unless the
   * table is initialize-once, use-many.
   * @param initial a vector of key->value pairs.
   */
  void compile(std::vector<KV> initial) {
    if (initial.empty()) {
      return;
    }
    size_t longest = 0;
    for (const KV& pair : initial) {
      longest = std::max(pair.first.size(), longest);
    }
    table_.resize(longest + 1);
    std::sort(initial.begin(), initial.end(),
              [](const KV& a, const KV& b) { return a.first.size() < b.first.size(); });
    auto it = initial.begin();
    for (size_t i = 0; i <= longest; i++) {
      auto start = it;
      while (it != initial.end() && it->first.size() == i) {
        it++;
      }
      if (it != start) {
        std::vector<KV> node_contents;
        node_contents.reserve(it - start);
        std::copy(start, it, std::back_inserter(node_contents));
        table_[i] = createEqualLengthNode(node_contents);
      }
    }
  }

private:
  static FindFn createEqualLengthNode(std::vector<KV> node_contents) {
    if (node_contents.size() == 1) {
      return [pair = node_contents[0]](const absl::string_view& key) -> Value {
        if (key != pair.first) {
          return {};
        }
        return pair.second;
      };
    }
    struct IndexSplitInfo {
      uint8_t index, min, max, count;
    } best{0, 0, 0, 0};
    for (size_t i = 0; i < node_contents[0].first.size(); i++) {
      std::array<bool, 256> hits{};
      IndexSplitInfo info{static_cast<uint8_t>(i), 255, 0, 0};
      for (size_t j = 0; j < node_contents.size(); j++) {
        uint8_t v = node_contents[j].first[i];
        if (!hits[v]) {
          hits[v] = true;
          info.count++;
          info.min = std::min(v, info.min);
          info.max = std::max(v, info.max);
        }
      }
      if (info.count > best.count) {
        best = info;
      }
    }
    std::vector<FindFn> nodes;
    nodes.resize(best.max - best.min + 1);
    std::sort(node_contents.begin(), node_contents.end(), [&best](const KV& a, const KV& b) {
      return a.first[best.index] < b.first[best.index];
    });
    auto it = node_contents.begin();
    for (int i = best.min; i <= best.max; i++) {
      auto start = it;
      while (it != node_contents.end() && it->first[best.index] == i) {
        it++;
      }
      if (it != start) {
        // Optimization was tried here, std::array<KV, 256> rather than
        // a smaller-range vector with bounds, to keep locality and reduce
        // comparisons. It didn't help.
        std::vector<KV> next_contents;
        next_contents.reserve(it - start);
        std::copy(start, it, std::back_inserter(next_contents));
        nodes[i - best.min] = createEqualLengthNode(next_contents);
      }
    }
    return [nodes = std::move(nodes), min = best.min,
            index = best.index](const absl::string_view& key) -> Value {
      uint8_t k = static_cast<uint8_t>(key[index]);
      // Possible optimization was tried here, populating empty nodes with
      // a function that returns {} to reduce branching vs checking for null
      // nodes. Checking for null nodes benchmarked faster.
      if (k < min || k >= min + nodes.size() || nodes[k - min] == nullptr) {
        return {};
      }
      return nodes[k - min](key);
    };
  }
  std::vector<FindFn> table_;
};

} // namespace Envoy
