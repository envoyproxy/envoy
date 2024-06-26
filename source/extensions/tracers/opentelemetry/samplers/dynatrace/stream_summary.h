#pragma once

#include <algorithm>
#include <cstdint>
#include <list>

#include "source/common/common/assert.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

namespace detail {

template <typename T> struct Bucket;

template <typename T> using BucketIterator = typename std::list<Bucket<T>>::iterator;

template <typename T> struct Counter {
  BucketIterator<T> bucket;
  absl::optional<T> item{};
  uint64_t value{};
  uint64_t error{};

  explicit Counter(BucketIterator<T> bucket) : bucket(bucket) {}
  Counter(Counter const&) = delete;
  Counter& operator=(Counter const&) = delete;
};

template <typename T> using CounterIterator = typename std::list<Counter<T>>::iterator;

template <typename T> struct Bucket {
  uint64_t value;
  std::list<Counter<T>> children{};

  explicit Bucket(uint64_t value) : value(value) {}
  Bucket(Bucket const&) = delete;
  Bucket& operator=(Bucket const&) = delete;
};

} // namespace detail

template <typename T> class Counter {
private:
  T const& item_;
  uint64_t value_;
  uint64_t error_;

public:
  Counter(detail::Counter<T> const& c) : item_(*c.item), value_(c.value), error_(c.error) {}

  T const& getItem() const { return item_; }
  uint64_t getValue() const { return value_; }
  uint64_t getError() const { return error_; }
};

/**
 * @brief Space Saving algorithm implementation also know as "HeavyHitter".
 * based on the "Space Saving algorithm", AKA "HeavyHitter"
 * See:
 * https://cse.hkust.edu.hk/~raywong/comp5331/References/EfficientComputationOfFrequentAndTop-kElementsInDataStreams.pdf
 * https://github.com/fzakaria/space-saving/tree/master
 *
 */
template <typename T> class StreamSummary {
private:
  const size_t capacity_;
  uint64_t n_{};
  absl::flat_hash_map<T, detail::CounterIterator<T>> cache_{};
  std::list<detail::Bucket<T>> buckets_{};

  typename detail::CounterIterator<T> incrementCounter(detail::CounterIterator<T> counter_iter,
                                                       uint64_t increment) {
    auto const bucket = counter_iter->bucket;
    auto bucket_next = std::prev(bucket);
    counter_iter->value += increment;

    detail::CounterIterator<T> elem;
    if (bucket_next != buckets_.end() && counter_iter->value == bucket_next->value) {
      counter_iter->bucket = bucket_next;
      bucket_next->children.splice(bucket_next->children.end(), bucket->children, counter_iter);
      elem = std::prev(bucket_next->children.end());
    } else {
      auto bucket_new = buckets_.emplace(bucket, counter_iter->value);
      counter_iter->bucket = bucket_new;
      bucket_new->children.splice(bucket_new->children.end(), bucket->children, counter_iter);
      elem = std::prev(bucket_new->children.end());
    }
    if (bucket->children.empty()) {
      buckets_.erase(bucket);
    }
    return elem;
  }

  absl::Status validateInternal() const {
    auto cache_copy = cache_;
    auto current_bucket = buckets_.begin();
    uint64_t value_sum = 0;
    while (current_bucket != buckets_.end()) {
      auto prev = std::prev(current_bucket);
      if (prev != buckets_.end() && prev->value <= current_bucket->value) {
        return absl::InternalError("buckets should be in descending order.");
      }
      auto current_child = current_bucket->children.begin();
      while (current_child != current_bucket->children.end()) {
        if (current_child->bucket != current_bucket ||
            current_child->value != current_bucket->value) {
          return absl::InternalError("entry does not point to a bucket with the same value.");
        }
        if (current_child->item) {
          auto old_iter = cache_copy.find(*current_child->item);
          if (old_iter != cache_copy.end()) {
            cache_copy.erase(old_iter);
          }
        }
        value_sum += current_child->value;
        current_child++;
      }
      current_bucket++;
    }
    if (!cache_copy.empty() || cache_.size() > capacity_ || value_sum != n_) {
      return absl::InternalError("unexpected size.");
    }
    return absl::OkStatus();
  }

  inline void validateDbg() {
#if !defined(NDEBUG)
    ASSERT(validate().ok());
#endif
  }

public:
  explicit StreamSummary(size_t capacity) : capacity_(capacity) {
    auto& new_bucket = buckets_.emplace_back(0);
    for (size_t i = 0; i < capacity; ++i) {
      // initialize with empty counters, optional item will not be set
      new_bucket.children.emplace_back(buckets_.begin());
    }
    validateDbg();
  }

  size_t getCapacity() const { return capacity_; }

  absl::Status validate() const { return validateInternal(); }

  Counter<T> offer(T const& item, uint64_t increment = 1) {
    ++n_;
    auto iter = cache_.find(item);
    if (iter != cache_.end()) {
      iter->second = incrementCounter(iter->second, increment);
      validateDbg();
      return *iter->second;
    } else {
      auto min_element = std::prev(buckets_.back().children.end());
      auto original_min_value = min_element->value;
      if (min_element
              ->item) { // element was already used (otherwise optional item would be not set)
        // remove old from cache
        auto old_iter = cache_.find(*min_element->item);
        if (old_iter != cache_.end()) {
          cache_.erase(old_iter);
        }
      }
      min_element->item = item;
      min_element = incrementCounter(min_element, increment);
      cache_[item] = min_element;
      if (cache_.size() <= capacity_) {
        // should always be true, but keep it to be aligned to reference implementation
        // originalMinValue will be 0 if element wasn't already used
        min_element->error = original_min_value;
      }
      validateDbg();
      return *min_element;
    }
  }

  uint64_t getN() const { return n_; }

  typename std::list<Counter<T>> getTopK(size_t k = SIZE_MAX) const {
    std::list<Counter<T>> r;
    for (auto const& bucket : buckets_) {
      for (auto const& child : bucket.children) {
        if (child.item) {
          r.emplace_back(child);
          if (r.size() == k) {
            return r;
          }
        }
      }
    }
    return r;
  }
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
