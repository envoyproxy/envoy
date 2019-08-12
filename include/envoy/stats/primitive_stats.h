#pragma once

#include <string>

#include "common/common/assert.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Primitive, low-memory-overhead counter with incrementing and latching capabilities. Each
 * increment is added both to a global counter as well as periodic counter. Calling latch()
 * returns the periodic counter and clears it.
 */
class PrimitiveCounter {
public:
  PrimitiveCounter() = default;
  // Disable copy constructors.
  PrimitiveCounter(const PrimitiveCounter&) = delete;
  PrimitiveCounter& operator=(const PrimitiveCounter&) = delete;

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

/**
 * Primitive, low-memory-overhead gauge with increment and decrement capabilities.
 */
class PrimitiveGauge {
public:
  PrimitiveGauge() = default;
  // Disable copy constructors.
  PrimitiveGauge(const PrimitiveGauge&) = delete;
  PrimitiveGauge& operator=(const PrimitiveGauge&) = delete;

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

} // namespace Stats
} // namespace Envoy
