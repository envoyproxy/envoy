#pragma once

#include <algorithm>
#include <iostream>
#include <vector>

#include "source/common/common/assert.h"

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Network {
namespace IntervalTree {

template <class Data, class N> class IntervalTree {
public:
  /**
   * @param data supplies a list of data and possibly overlapping number intervals [start, end).
   */
  IntervalTree(const std::vector<std::tuple<Data, N, N>>& data) {
    ASSERT(!data.empty(), "Must supply non-empty list.");
    // Create a scaffold tree from medians in the set of all start
    // and end points of the intervals to ensure the tree is balanced.
    std::vector<N> medians;
    medians.reserve(data.size() * 2);
    for (const auto [_, start, end] : data) {
      ASSERT(start < end, "Interval must be properly formed.");
      medians.push_back(start);
      medians.push_back(end);
    }
    std::sort(medians.begin(), medians.end());
    root_ = std::make_unique<Node>();
    root_->populate(medians);
    size_t rank = 0;
    // Insert intervals and recursively prune and order intervals in each node.
    for (const auto& datum : data) {
      root_->insert(datum, rank++);
    }
    root_->pruneAndOrder();
  }
  /**
   * Returns the data of intervals containing the query number in the original list order.
   **/
  std::vector<Data> getData(N query) {
    std::vector<RankedInterval*> result;
    root_->search(query, result);
    std::sort(result.begin(), result.end(),
              [](const auto* lhs, const auto* rhs) { return lhs->rank_ < rhs->rank_; });
    std::vector<Data> out;
    out.reserve(result.size());
    for (auto* elt : result) {
      out.push_back(elt->data_);
    }
    return out;
  }

private:
  using Interval = std::tuple<Data, N, N>;
  struct RankedInterval {
    RankedInterval(const Interval& datum, size_t rank)
        : data_(std::get<0>(datum)), rank_(rank), start_(std::get<1>(datum)),
          end_(std::get<2>(datum)) {}
    Data data_;
    size_t rank_;
    N start_;
    N end_;
    // Intervals are linked in their increasing start order and decreasing end order.
    RankedInterval* next_start_{nullptr};
    RankedInterval* next_end_{nullptr};
  };
  /**
   * Nodes in the tree satisfy the following invariants:
   * * Intervals in the node contain the median value.
   * * Intervals in the left sub tree have the end point less or equal to the median value.
   * * Intervals in the right sub tree have the start point greater than the median value.
   * * Intervals in the node are linked from the lowest start in the ascending order.
   * * Intervals in the node are linked from the highest end in the descending order.
   **/
  struct Node {
    N median_;
    std::unique_ptr<Node> left_;
    std::unique_ptr<Node> right_;
    // Ranked intervals are sorted by position in the input vector.
    std::vector<RankedInterval> intervals_;
    // Interval with the lowest start.
    RankedInterval* low_start_;
    // Interval with the highest end.
    RankedInterval* high_end_;

    void populate(absl::Span<const N> span) {
      const size_t size = span.size();
      const size_t mid = size >> 1;
      median_ = span[mid];
      // Last value equal to median on the left.
      size_t left = mid;
      while (left > 0 && span[left - 1] == median_) {
        left--;
      }
      if (left > 0) {
        left_ = std::make_unique<Node>();
        left_->populate(span.subspan(0, left));
      }
      // Last value equal to median on the right.
      size_t right = mid;
      while (right < size - 1 && span[right + 1] == median_) {
        right++;
      }
      if (right < size - 1) {
        right_ = std::make_unique<Node>();
        right_->populate(span.subspan(right + 1));
      }
    }
    void insert(const Interval& datum, size_t rank) {
      N start = std::get<1>(datum);
      N end = std::get<2>(datum);
      if (end <= median_) {
        left_->insert(datum, rank);
      } else if (median_ < start) {
        right_->insert(datum, rank);
      } else {
        intervals_.emplace_back(datum, rank);
      }
    }
    bool pruneAndOrder() {
      bool left_empty = true;
      if (left_) {
        left_empty = left_->pruneAndOrder();
        if (left_empty) {
          left_ = nullptr;
        }
      }
      bool right_empty = true;
      if (right_) {
        right_empty = right_->pruneAndOrder();
        if (right_empty) {
          right_ = nullptr;
        }
      }
      if (!intervals_.empty()) {
        linkStart();
        linkEnd();
        return false;
      }
      return left_empty && right_empty;
    }
    void linkStart() {
      std::vector<RankedInterval*> sorted;
      sorted.reserve(intervals_.size());
      for (auto& elt : intervals_) {
        sorted.push_back(&elt);
      }
      std::sort(sorted.begin(), sorted.end(),
                [](const auto* lhs, const auto* rhs) { return lhs->start_ < rhs->start_; });
      for (size_t i = 0; i < sorted.size() - 1; i++) {
        sorted[i]->next_start_ = sorted[i + 1];
      }
      low_start_ = sorted[0];
    }
    void linkEnd() {
      std::vector<RankedInterval*> sorted;
      sorted.reserve(intervals_.size());
      for (auto& elt : intervals_) {
        sorted.push_back(&elt);
      }
      std::sort(sorted.begin(), sorted.end(),
                [](const auto* lhs, const auto* rhs) { return lhs->end_ > rhs->end_; });
      for (size_t i = 0; i < sorted.size() - 1; i++) {
        sorted[i]->next_end_ = sorted[i + 1];
      }
      high_end_ = sorted[0];
    }
    void search(N query, std::vector<RankedInterval*>& result) {
      // Always search within the node.
      if (query <= median_) {
        auto* cur = low_start_;
        while (cur && cur->start_ <= query) {
          result.push_back(cur);
          cur = cur->next_start_;
        }
        if (query < median_ && left_) {
          left_->search(query, result);
        }
      } else {
        auto* cur = high_end_;
        while (cur && cur->end_ > query) {
          result.push_back(cur);
          cur = cur->next_end_;
        }
        if (right_) {
          right_->search(query, result);
        }
      }
    }
  };
  using NodePtr = std::unique_ptr<Node>;
  NodePtr root_;
};

} // namespace IntervalTree
} // namespace Network
} // namespace Envoy
