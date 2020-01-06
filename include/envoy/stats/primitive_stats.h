#pragma once

#include <string>

#include "common/common/assert.h"
#include "common/common/non_copyable.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

class PrimitiveCounter {
public:
  virtual ~PrimitiveCounter() = default;

  virtual uint64_t value() const PURE;

  virtual void add(uint64_t) PURE;
  virtual void inc() PURE;
  virtual void reset() PURE;
  virtual uint64_t latch() PURE;
};

class NullPrimitiveCounterImpl : public PrimitiveCounter {
public:
  ~NullPrimitiveCounterImpl() override = default;

  uint64_t value() const override { return 0; }

  void add(uint64_t) override {}
  void inc() override {}
  void reset() override {}
  uint64_t latch() override { return 0; }
};

/**
 * Primitive, low-memory-overhead counter with incrementing and latching capabilities. Each
 * increment is added both to a global counter as well as periodic counter. Calling latch()
 * returns the periodic counter and clears it.
 */
class PrimitiveCounterImpl : NonCopyable, public PrimitiveCounter {
public:
  ~PrimitiveCounterImpl() override = default;

  uint64_t value() const override { return value_; }

  void add(uint64_t amount) override {
    value_ += amount;
    pending_increment_ += amount;
  }
  void inc() override { add(1); }
  void reset() override { value_ = 0; }
  uint64_t latch() override { return pending_increment_.exchange(0); }

private:
  std::atomic<uint64_t> value_{0};
  std::atomic<uint64_t> pending_increment_{0};
};

using PrimitiveCounterReference = std::reference_wrapper<const PrimitiveCounter>;

class PrimitiveGauge {
public:
  virtual ~PrimitiveGauge() = default;
  virtual uint64_t value() const PURE;

  virtual void add(uint64_t) PURE;
  virtual void dec() PURE;
  virtual void inc() PURE;
  virtual void set(uint64_t) PURE;
  virtual void sub(uint64_t) PURE;
};

class NullPrimitiveGaugeImpl : public PrimitiveGauge {
public:
  ~NullPrimitiveGaugeImpl() override = default;
  uint64_t value() const override { return 0; }

  void add(uint64_t) override {}
  void dec() override {}
  void inc() override {}
  void set(uint64_t) override {}
  void sub(uint64_t) override {}
};

/**
 * Primitive, low-memory-overhead gauge with increment and decrement capabilities.
 */
class PrimitiveGaugeImpl : NonCopyable, public PrimitiveGauge {
public:
  ~PrimitiveGaugeImpl() override = default;

  uint64_t value() const override { return value_; }

  void add(uint64_t amount) override { value_ += amount; }
  void dec() override { sub(1); }
  void inc() override { add(1); }
  void set(uint64_t value) override { value_ = value; }
  void sub(uint64_t amount) override {
    ASSERT(value_ >= amount);
    value_ -= amount;
  }

private:
  std::atomic<uint64_t> value_{0};
};

using PrimitiveGaugeReference = std::reference_wrapper<const PrimitiveGauge>;

} // namespace Stats
} // namespace Envoy
