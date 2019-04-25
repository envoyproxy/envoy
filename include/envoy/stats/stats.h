#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

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
   * contain the name as a std::string.
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

  /**
   * Flags:
   * Used: used by all stats types to figure out whether they have been used.
   * Logic...: used by gauges to cache how they should be combined with a parent's value.
   */
  struct Flags {
    static const uint8_t Used = 0x01;
    static const uint8_t LogicAccumulate = 0x02;
    static const uint8_t LogicUnusedOnly = 0x04;
    static const uint8_t LogicNeverImport = 0x08;
    static const uint8_t LogicKnown = LogicAccumulate | LogicUnusedOnly | LogicNeverImport;
  };
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

  // Different approaches to importing a parent's stat value. Only used for gauges; all counters
  // simply bring in the periodic deltas.
  enum class CombineLogic {
    // the default; the merged result is old+new.
    Accumulate = Flags::LogicAccumulate,
    // import parent value only if child stat is undefined. (So, just once.)
    OnlyImportWhenUnusedInChild = Flags::LogicUnusedOnly,
    // ignore parent entirely; child stat is undefined until it sets its own value.
    NoImport = Flags::LogicNeverImport,
  };

  /**
   * Returns the stat's combine logic, if known.
   */
  virtual absl::optional<CombineLogic> cachedCombineLogic() const PURE;

  /**
   * Sets the value to be returned by cachedCombineLogic().
   */
  virtual void setCombineLogic(CombineLogic logic) PURE;
};

typedef std::shared_ptr<Gauge> GaugeSharedPtr;

} // namespace Stats
} // namespace Envoy
