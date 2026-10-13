#pragma once

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

#include "source/common/common/assert.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {

/**
 * This is a specialized structure intended for static header maps, but
 * there may be other use cases.
 *
 * See `compiled_string_map.md` for details.
 */
template <class Value> class CompiledStringMap {
  class Node {
  public:
    // While it is usual to take a string_view by value, in this
    // performance-critical context with repeatedly passing the same
    // value, passing it by reference benchmarks out slightly faster.
    virtual Value find(const absl::string_view& key) PURE;
    virtual ~Node() = default;
  };

  class LeafNode : public Node {
  public:
    LeafNode(absl::string_view key, Value&& value) : key_(key), value_(std::move(value)) {}
    Value find(const absl::string_view& key) override {
      // String comparison unnecessarily checks size equality first, we can skip
      // to memcmp here because we already know the sizes are equal.
      // Since this is a super-hot path we don't even ASSERT here, to avoid adding
      // slowdown in debug builds.
      if (memcmp(key.data(), key_.data(), key.size())) {
        return {};
      }
      return value_;
    }

  private:
    const std::string key_;
    const Value value_;
  };

  class BranchNode : public Node {
  public:
    BranchNode(size_t index, uint8_t min, std::vector<std::unique_ptr<Node>>&& branches)
        : index_(index), min_(min), branches_(std::move(branches)) {}
    Value find(const absl::string_view& key) override {
      const uint8_t k = static_cast<uint8_t>(key[index_]);
      // Possible optimization was tried here, populating empty nodes with
      // a function that returns {} to reduce branching vs checking for null
      // nodes. Checking for null nodes benchmarked faster.
      if (k < min_ || k >= min_ + branches_.size() || branches_[k - min_] == nullptr) {
        return {};
      }
      return branches_[k - min_]->find(key);
    }

  private:
    const size_t index_;
    const uint8_t min_;
    // Possible optimization was tried here, using std::array<std::unique_ptr<Node>, 256>
    // rather than a smaller-range vector with bounds, to keep locality and reduce
    // comparisons. It didn't help.
    const std::vector<std::unique_ptr<Node>> branches_;
  };

public:
  // The caller owns the string-views during `compile`. Ownership of the passed in
  // Values is transferred to the CompiledStringMap.
  using KV = std::pair<absl::string_view, Value>;
  /**
   * Returns the value with a matching key, or the default value
   * (typically nullptr) if the key was not present.
   * @param key the key to look up.
   */
  Value find(absl::string_view key) const {
    const size_t key_size = key.size();
    // Theoretically we could also bottom-cap the size range, but the
    // cost of the extra comparison and operation would almost certainly
    // outweigh the benefit of omitting 4 or 5 entries.
    if (key_size >= table_.size() || table_[key_size] == nullptr) {
      return {};
    }
    return table_[key_size]->find(key);
  };
  /**
   * Construct the lookup table. This can be a somewhat slow multi-pass
   * operation if the input table is large.
   * @param contents a vector of key->value pairs. This is taken by value because
   *                 we're going to modify it. If the caller still wants the original
   *                 then it can be copied in, if not it can be moved in.
   *                 Note that the keys are string_views - the base string data must
   *                 exist for the duration of compile(). The leaf nodes take copies
   *                 of the key strings, so the string_views can be invalidated once
   *                 compile has completed.
   */
  void compile(std::vector<KV> contents) {
    if (contents.empty()) {
      return;
    }
    std::sort(contents.begin(), contents.end(),
              [](const KV& a, const KV& b) { return a.first.size() < b.first.size(); });
    const size_t longest = contents.back().first.size();
    // A key length of 0 is possible, and also we don't want to have to
    // subtract [min length] every time we index, so the table size must
    // be one larger than the longest key.
    table_.resize(longest + 1);
    auto range_start = contents.begin();
    // Populate the sub-nodes for each length of key that exists.
    while (range_start != contents.end()) {
      // Find the first key whose length differs from the current key length.
      // Everything in between is keys with the same length.
      const auto range_end =
          std::find_if(range_start, contents.end(), [len = range_start->first.size()](const KV& e) {
            return e.first.size() != len;
          });
      std::vector<KV> node_contents;
      // Populate a Node for the entries in that range.
      node_contents.reserve(range_end - range_start);
      std::move(range_start, range_end, std::back_inserter(node_contents));
      table_[range_start->first.size()] = createEqualLengthNode(node_contents);
      range_start = range_end;
    }
  }

private:
  /**
   * Details of a node branch point; the index into the string at which
   * characters should be looked up, the lowest valued character in the
   * branch, the highest valued character in the branch, and how many
   * branches there are.
   */
  struct IndexSplitInfo {
    // The index to the character being considered for this split.
    size_t index_;
    // The smallest character value that appears at this index.
    uint8_t min_;
    // The largest character value that appears at this index.
    uint8_t max_;
    // The number of distinct characters that appear at this index.
    uint8_t count_;
    size_t size() const { return max_ - min_ + 1; }
    size_t offsetOf(uint8_t c) const { return c - min_; }
  };

  /**
   * @param node_contents the key-value pairs to be branched upon.
   * @return details of the index on which the node should branch
   *         - the index which produces the most child branches.
   */
  static IndexSplitInfo findBestSplitPoint(const std::vector<KV>& node_contents) {
    ASSERT(node_contents.size() > 1);
    IndexSplitInfo best{0, 0, 0, 0};
    const size_t key_length = node_contents[0].first.size();
    for (size_t i = 0; i < key_length; i++) {
      std::array<bool, 256> hits{};
      IndexSplitInfo info{static_cast<uint8_t>(i), 255, 0, 0};
      for (const KV& pair : node_contents) {
        uint8_t v = pair.first[i];
        if (!hits[v]) {
          hits[v] = true;
          info.count_++;
          info.min_ = std::min(v, info.min_);
          info.max_ = std::max(v, info.max_);
        }
      }
      if (info.count_ > best.count_) {
        best = info;
      }
    }
    ASSERT(best.count_ > 1, absl::StrCat("duplicate key: ", node_contents[0].first));
    return best;
  }

  /*
   * @param node_contents the set of key-value pairs that will be children of
   *                      this node.
   * @return the recursively generated tree node that leads to all of node_contents.
   *         If there is only one entry in node_contents then a LeafNode, otherwise a BranchNode.
   */
  static std::unique_ptr<Node> createEqualLengthNode(std::vector<KV> node_contents) {
    if (node_contents.size() == 1) {
      return std::make_unique<LeafNode>(node_contents[0].first, std::move(node_contents[0].second));
    }
    // best contains the index at which this node should be split,
    // and the smallest and largest character values that occur at
    // that index across all the keys in node_contents.
    const IndexSplitInfo best = findBestSplitPoint(node_contents);
    std::vector<std::unique_ptr<Node>> nodes;
    nodes.resize(best.size());
    std::sort(node_contents.begin(), node_contents.end(),
              [index = best.index_](const KV& a, const KV& b) {
                return a.first[index] < b.first[index];
              });
    auto range_start = node_contents.begin();
    // Populate the sub-nodes for each character-branch.
    while (range_start != node_contents.end()) {
      // Find the first key whose character at position [best.index_] differs from the
      // character of the current range.
      // Everything in the range has keys with the same character at this index.
      auto range_end = std::find_if(range_start, node_contents.end(),
                                    [index = best.index_, c = range_start->first[best.index_]](
                                        const KV& e) { return e.first[index] != c; });
      std::vector<KV> next_contents;
      next_contents.reserve(range_end - range_start);
      std::move(range_start, range_end, std::back_inserter(next_contents));
      nodes[best.offsetOf(range_start->first[best.index_])] = createEqualLengthNode(next_contents);
      range_start = range_end;
    }
    return std::make_unique<BranchNode>(best.index_, best.min_, std::move(nodes));
  }
  std::vector<std::unique_ptr<Node>> table_;
};

} // namespace Envoy
