#pragma once

#include <string>

#include "envoy/stats/tag.h"

#include "source/common/common/assert.h"
#include "source/common/common/non_copyable.h"

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

using PrimitiveCounterReference = std::reference_wrapper<PrimitiveCounter>;

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

using PrimitiveGaugeReference = std::reference_wrapper<PrimitiveGauge>;

class PrimitiveMetricMetadata {
public:
  // Mirror some of the API for Stats::Metric for use in templates that
  // accept either Counter/Gauge or PrimitiveCounterSnapshot/PrimitiveGaugeSnapshot.
  const std::string& tagExtractedName() const { return tag_extracted_name_; }
  const std::string& name() const { return name_; }
  const Stats::TagVector& tags() const { return tags_; }
  bool used() const { return true; }
  bool hidden() const { return false; }

  void setName(std::string&& name) { name_ = std::move(name); }
  void setTagExtractedName(std::string&& tag_extracted_name) {
    tag_extracted_name_ = std::move(tag_extracted_name);
  }
  void setTags(const Stats::TagVector& tags) { tags_ = tags; }

private:
  std::string name_;
  std::string tag_extracted_name_;
  Stats::TagVector tags_;
};

class PrimitiveCounterSnapshot : public PrimitiveMetricMetadata {
public:
  PrimitiveCounterSnapshot(PrimitiveCounter& counter)
      : value_(counter.value()), delta_(counter.latch()) {}

  uint64_t value() const { return value_; }
  uint64_t delta() const { return delta_; }

private:
  const uint64_t value_;
  const uint64_t delta_;
};

class PrimitiveGaugeSnapshot : public PrimitiveMetricMetadata {
public:
  PrimitiveGaugeSnapshot(PrimitiveGauge& gauge) : value_(gauge.value()) {}

  uint64_t value() const { return value_; }

private:
  const uint64_t value_;
};

} // namespace Stats
} // namespace Envoy
