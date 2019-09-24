#pragma once

#include <string>

#include "common/common/assert.h"
#include "common/common/non_copyable.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Primitive, low-memory-overhead counter with incrementing and latching capabilities. Each
 * increment is added both to a global counter as well as periodic counter. Calling latch()
 * returns the periodic counter and clears it.
 */
class PrimitiveCounter : NonCopyable {
public:
  PrimitiveCounter() = default;

  uint64_t value() const { return value_; }

  void add(uint64_t amount) {
    value_ += amount;
    pending_increment_ += amount;
  }
  void inc() { add(1); }
  void reset() { value_ = 0; }
  uint64_t latch() { return pending_increment_.exchange(0); }

private:
  std::atomic<uint64_t> value_{0};
  std::atomic<uint64_t> pending_increment_{0};
};

using PrimitiveCounterReference = std::reference_wrapper<const PrimitiveCounter>;

/**
 * Primitive, low-memory-overhead gauge with increment and decrement capabilities.
 */
class PrimitiveGauge : NonCopyable {
public:
  PrimitiveGauge() = default;

  uint64_t value() const { return value_; }

  void add(uint64_t amount) { value_ += amount; }
  void dec() { sub(1); }
  void inc() { add(1); }
  void set(uint64_t value) { value_ = value; }
  void sub(uint64_t amount) {
    ASSERT(value_ >= amount);
    value_ -= amount;
  }

private:
  std::atomic<uint64_t> value_{0};
};

using PrimitiveGaugeReference = std::reference_wrapper<const PrimitiveGauge>;

} // namespace Stats
} // namespace Envoy
