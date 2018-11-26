#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

struct Tag;

/**
 * General interface for all stats objects.
 */
class Metric {
public:
  virtual ~Metric() {}
  /**
   * Returns the full name of the Metric. This is intended for most uses, such
   * as streaming out the name to a stats sink or admin request, or comparing
   * against it in a test. Independent of the evolution of the data
   * representation for the name, this method will be available. For storing the
   * name as a map key, however, nameCStr() is a better choice, albeit one that
   * might change in the future to return a symbolized representation of the
   * elaborated string.
   */
  virtual std::string name() const PURE;

  /**
   * Returns the full name of the Metric as a nul-terminated string. The
   * intention is use this as a hash-map key, so that the stat name storage
   * is not duplicated in every map. You cannot use name() above for this,
   * as it returns a std::string by value, as not all stat implementations
   * containe the name as a std::string.
   *
   * Note that in the future, the plan is to replace this method with one that
   * returns a reference to a symbolized representation of the elaborated string
   * (see source/common/stats/symbol_table_impl.h).
   */
  virtual const char* nameCStr() const PURE;

  /**
   * Returns a vector of configurable tags to identify this Metric.
   */
  virtual const std::vector<Tag>& tags() const PURE;

  /**
   * Returns the name of the Metric with the portions designated as tags removed.
   */
  virtual const std::string& tagExtractedName() const PURE;

  /**
   * Indicates whether this metric has been updated since the server was started.
   */
  virtual bool used() const PURE;
};

/**
 * An always incrementing counter with latching capability. Each increment is added both to a
 * global counter as well as periodic counter. Calling latch() returns the periodic counter and
 * clears it.
 */
class Counter : public virtual Metric {
public:
  virtual ~Counter() {}
  virtual void add(uint64_t amount) PURE;
  virtual void inc() PURE;
  virtual uint64_t latch() PURE;
  virtual void reset() PURE;
  virtual uint64_t value() const PURE;
};

typedef std::shared_ptr<Counter> CounterSharedPtr;

/**
 * A gauge that can both increment and decrement.
 */
class Gauge : public virtual Metric {
public:
  virtual ~Gauge() {}

  virtual void add(uint64_t amount) PURE;
  virtual void dec() PURE;
  virtual void inc() PURE;
  virtual void set(uint64_t value) PURE;
  virtual void sub(uint64_t amount) PURE;
  virtual uint64_t value() const PURE;
};

typedef std::shared_ptr<Gauge> GaugeSharedPtr;

} // namespace Stats
} // namespace Envoy
