#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/stats/symbol_table.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Stats {

class StatDataAllocator;
struct Tag;

/**
 * General interface for all stats objects.
 */
class Metric {
public:
  virtual ~Metric() = default;
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
   * Returns the full name of the Metric as an encoded array of symbols.
   */
  virtual StatName statName() const PURE;

  /**
   * Returns a vector of configurable tags to identify this Metric.
   */
  virtual std::vector<Tag> tags() const PURE;

  /**
   * Returns the name of the Metric with the portions designated as tags removed
   * as a string. For example, The stat name "vhost.foo.vcluster.bar.c1" would
   * have "foo" extracted as the value of tag "vhost" and "bar" extracted as the
   * value of tag "vcluster". Thus the tagExtractedName is simply
   * "vhost.vcluster.c1".
   *
   * @return The stat name with all tag values extracted.
   */
  virtual std::string tagExtractedName() const PURE;

  /**
   * Returns the name of the Metric with the portions designated as tags
   * removed as a StatName
   */
  virtual StatName tagExtractedStatName() const PURE;

  // Function to be called from iterateTagStatNames passing name and value as StatNames.
  using TagStatNameIterFn = std::function<bool(StatName, StatName)>;

  /**
   * Iterates over all tags, calling a functor for each name/value pair. The
   * functor can return 'true' to continue or 'false' to stop the
   * iteration.
   *
   * @param fn The functor to call for StatName pair.
   */
  virtual void iterateTagStatNames(const TagStatNameIterFn& fn) const PURE;

  // Function to be called from iterateTags passing name and value as const Tag&.
  using TagIterFn = std::function<bool(const Tag&)>;

  /**
   * Iterates over all tags, calling a functor for each one. The
   * functor can return 'true' to continue or 'false' to stop the
   * iteration.
   *
   * @param fn The functor to call for each Tag.
   */
  virtual void iterateTags(const TagIterFn& fn) const PURE;

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
    static const uint8_t NeverImport = 0x04;
  };
  virtual SymbolTable& symbolTable() PURE;
  virtual const SymbolTable& constSymbolTable() const PURE;
};

/**
 * An always incrementing counter with latching capability. Each increment is added both to a
 * global counter as well as periodic counter. Calling latch() returns the periodic counter and
 * clears it.
 */
class Counter : public virtual Metric {
public:
  ~Counter() override = default;
  virtual void add(uint64_t amount) PURE;
  virtual void inc() PURE;
  virtual uint64_t latch() PURE;
  virtual void reset() PURE;
  virtual uint64_t value() const PURE;
};

using CounterSharedPtr = std::shared_ptr<Counter>;

/**
 * A gauge that can both increment and decrement.
 */
class Gauge : public virtual Metric {
public:
  enum class ImportMode {
    Uninitialized, // Gauge was discovered during hot-restart transfer.
    NeverImport,   // On hot-restart, each process starts with gauge at 0.
    Accumulate,    // Transfers gauge state on hot-restart.
  };

  ~Gauge() override = default;

  virtual void add(uint64_t amount) PURE;
  virtual void dec() PURE;
  virtual void inc() PURE;
  virtual void set(uint64_t value) PURE;
  virtual void sub(uint64_t amount) PURE;
  virtual uint64_t value() const PURE;

  /**
   * @return the import mode, dictating behavior of the gauge across hot restarts.
   */
  virtual ImportMode importMode() const PURE;

  /**
   * Gauges can be created with ImportMode::Uninitialized during hot-restart
   * merges, if they haven't yet been instantiated by the child process. When
   * they finally get instantiated, mergeImportMode should be called to
   * initialize the gauge's import mode. It is only valid to call
   * mergeImportMode when the current mode is ImportMode::Uninitialized.
   *
   * @param import_mode the new import mode.
   */
  virtual void mergeImportMode(ImportMode import_mode) PURE;
};

using GaugeSharedPtr = std::shared_ptr<Gauge>;

} // namespace Stats
} // namespace Envoy
