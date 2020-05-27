#pragma once

#include <limits>

#include "envoy/common/resource.h"
#include "envoy/runtime/runtime.h"

#include "common/common/assert.h"

#include "absl/types/optional.h"

namespace Envoy {

/**
 * A handle to track some limited resource.
 *
 * NOTE:
 * This implementation makes some assumptions which favor simplicity over correctness. Though
 * atomics are used, it is possible for resources to temporarily go above the supplied maximums.
 * This should not effect overall behavior.
 */
class BasicResourceLimitImpl : public ResourceLimit {
public:
  BasicResourceLimitImpl(uint64_t max, Runtime::Loader& runtime, const std::string& runtime_key)
      : max_(max), runtime_(&runtime), runtime_key_(runtime_key) {}
  BasicResourceLimitImpl(uint64_t max) : max_(max), runtime_(nullptr) {}
  BasicResourceLimitImpl() : max_(std::numeric_limits<uint64_t>::max()), runtime_(nullptr) {}

  bool canCreate() override { return current_.load() < max(); }

  void inc() override { ++current_; }

  void dec() override { decBy(1); }

  void decBy(uint64_t amount) override {
    ASSERT(current_ >= amount);
    current_ -= amount;
  }

  uint64_t max() override {
    return (runtime_ != nullptr && runtime_key_.has_value())
               ? runtime_->snapshot().getInteger(runtime_key_.value(), max_)
               : max_;
  }

  uint64_t count() const override { return current_.load(); }

  void setMax(uint64_t new_max) { max_ = new_max; }
  void resetMax() { max_ = std::numeric_limits<uint64_t>::max(); }

protected:
  std::atomic<uint64_t> current_{};

private:
  uint64_t max_;
  Runtime::Loader* runtime_{nullptr};
  const absl::optional<std::string> runtime_key_;
};

} // namespace Envoy
