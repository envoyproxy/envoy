#pragma once

#include <algorithm>
#include <vector>
#include "absl/types/optional.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Network {
namespace IntervalTree {

template <class Data, class N> class IntervalTree {
public:
  /**
   * @param data supplies a vector of data and possibly overlapping number intervals.
   */
  IntervalTree(const std::vector<std::tuple<Data, N, N>>& data) {
    ASSERT(!data.empty());
    // Create a scaffold tree from medians in the set of all start
    // and end points of the intervals.
    std::vector<N> medians;
    medians.reserve(data.size() * 2);
    for (const auto [_, start, end]: data) {
      medians.push_back(start);
      medians.push_back(end);
    }
    std::sort(medians.begin(), medians.end());
    root_ = std::make_unique<Node>();
    populateNodes(root_.get(), medians);
    size_t rank = 0;
    for (const auto& datum : data) {
      insertInterval(datum, rank++, root_.get());
    }
  }

private:
  using Interval = std::tuple<Data, N, N>;
  using RankedInterval = std::pair<Interval, size_t>;
  struct Node {
    N median_;
    std::unique_ptr<Node> children_[2];
    std::vector<RankedInterval> intervals_;
  };
  using NodePtr = std::unique_ptr<Node>;
  NodePtr root_;

  void populateNodes(Node* node, absl::Span<const N> span) {
    const size_t mid = span.size() >> 2;
    node->median_ = span[mid];
    auto left = span.subspan(0, mid);
    if (!left.empty()) {
      node->children_[0] = std::make_unique<Node>();
      populateNodes(node->children_[0].get(), left);
    }
    auto right = span.subspan(mid+1);
    if (!right.empty()) {
      node->children_[1] = std::make_unique<Node>();
      populateNodes(node->children_[1].get(), right);
    }
  }
  void insertInterval(const Interval& datum, size_t rank, Node* node) {
    N start = std::get<1>(datum);
    N end = std::get<2>(datum);
    if (end <= node->median_) {
      insertInterval(datum, rank, node->children[0].get());
    } else if (node->median_ < start) {
      insertInterval(datum, rank, node->children[1].get());
    } else {
      node->values_.push_back(std::make_pair(datum, rank));
    }
  }
};

} // namespace IntervalTree
} // namespace Network
} // namespace Envoy
