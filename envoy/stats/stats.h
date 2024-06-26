#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/stats/refcount_ptr.h"
#include "envoy/stats/tag.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Runtime representation of an encoded stat name.
 */
class StatName;

/**
 * Holds a set of symbols used to compose hierarchical names.
 */
class SymbolTable;

/**
 * General interface for all stats objects.
 *
 * Note: some methods must match those in `PrimitiveMetricMetadata` because stats sinks
 * use templates to handle either type. The interface is not used for size/performance
 * reasons.
 */
class Metric : public RefcountInterface {
public:
  ~Metric() override = default;
  /**
   * Returns the full name of the Metric. This is intended for most uses, such
   * as streaming out the name to a stats sink or admin request, or comparing
   * against it in a test. Independent of the evolution of the data
   * representation for the name, this method will be available.
   */
  virtual std::string name() const PURE;

  /**
   * Returns the full name of the Metric as an encoded array of symbols.
   */
  virtual StatName statName() const PURE;

  /**
   * Returns a vector of configurable tags to identify this Metric.
   */
  virtual TagVector tags() const PURE;

  /**
   * See a more detailed description in tagExtractedStatName(), which is the
   * preferred API to use when feasible. This API needs to compose the
   * std::string on the fly, and return it by value.
   *
   * @return The stat name with all tag values extracted, as a std::string.
   */
  virtual std::string tagExtractedName() const PURE;

  /**
   * Returns the name of the Metric with the portions designated as tags removed
   * as a string. For example, The stat name "vhost.foo.vcluster.bar.c1" would
   * have "foo" extracted as the value of tag "vhost" and "bar" extracted as the
   * value of tag "vcluster". Thus the tagExtractedName is simply
   * "vhost.vcluster.c1".
   *
   * @return the name of the Metric with the portions designated as tags
   *     removed.
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
   * Indicates whether this metric has been updated since the server was started.
   */
  virtual bool used() const PURE;

  /**
   * Indicates whether this metric is hidden.
   */
  virtual bool hidden() const PURE;

  /**
   * Flags:
   * Used: used by all stats types to figure out whether they have been used.
   * Logic...: used by gauges to cache how they should be combined with a parent's value.
   */
  struct Flags {
    static constexpr uint8_t Used = 0x01;
    static constexpr uint8_t LogicAccumulate = 0x02;
    static constexpr uint8_t NeverImport = 0x04;
    static constexpr uint8_t Hidden = 0x08;
  };
  virtual SymbolTable& symbolTable() PURE;
  virtual const SymbolTable& constSymbolTable() const PURE;
};

/**
 * An always incrementing counter with latching capability. Each increment is added both to a
 * global counter as well as periodic counter. Calling latch() returns the periodic counter and
 * clears it.
 */
class Counter : public Metric {
public:
  ~Counter() override = default;

  virtual void add(uint64_t amount) PURE;
  virtual void inc() PURE;
  virtual uint64_t latch() PURE;
  virtual void reset() PURE;
  virtual uint64_t value() const PURE;
};

using CounterSharedPtr = RefcountPtr<Counter>;

/**
 * A gauge that can both increment and decrement.
 */
class Gauge : public Metric {
public:
  // TODO(diazalan): Rename ImportMode to more generic name
  enum class ImportMode {
    Uninitialized,    // Gauge was discovered during hot-restart transfer.
    NeverImport,      // On hot-restart, each process starts with gauge at 0.
    Accumulate,       // Transfers gauge state on hot-restart.
    HiddenAccumulate, // Will be transferred on hot-restart and ignored by admin/stats-sink
  };

  ~Gauge() override = default;

  virtual void add(uint64_t amount) PURE;
  virtual void dec() PURE;
  virtual void inc() PURE;
  virtual void set(uint64_t value) PURE;
  virtual void sub(uint64_t amount) PURE;
  // When adjusting a gauge both ways (e.g. buffer size update when simultaneously
  // streaming in and out), using this helper avoids touching the atomic twice.
  inline void adjust(uint64_t add_amount, uint64_t sub_amount) {
    if (add_amount > sub_amount) {
      add(add_amount - sub_amount);
    } else {
      sub(sub_amount - add_amount);
    }
  }
  virtual uint64_t value() const PURE;

  /**
   * Sets a value from a hot-restart parent. This parent contribution must be
   * kept distinct from the child value, so that when we erase the value it
   * is not commingled with the child value, which may have been set() directly.
   *
   * @param parent_value the value from the hot-restart parent.
   */
  virtual void setParentValue(uint64_t parent_value) PURE;

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

using GaugeSharedPtr = RefcountPtr<Gauge>;

/**
 * A string, possibly non-ASCII.
 */
class TextReadout : public virtual Metric {
public:
  // Text readout type is used internally to disambiguate isolated store
  // constructors. In the future we can extend it to specify text encoding or
  // some such.
  enum class Type {
    Default, // No particular meaning.
  };

  ~TextReadout() override = default;

  /**
   * Sets the value of this TextReadout by moving the input |value| to minimize
   * buffer copies under the lock.
   */
  virtual void set(absl::string_view value) PURE;
  /**
   * @return the copy of this TextReadout value.
   */
  virtual std::string value() const PURE;
};

using TextReadoutSharedPtr = RefcountPtr<TextReadout>;

/**
 * Callback invoked to provide size of stats container.
 */
using SizeFn = std::function<void(std::size_t)>;

/**
 * Callback invoked for each stat during iteration.
 */
template <typename Stat> using StatFn = std::function<void(Stat&)>;

/**
 * Interface for stats lazy initialization.
 * To save memory and CPU consumption on blocks of stats that are never referenced throughout the
 * process lifetime, they can be encapsulated in a DeferredCreationCompatibleInterface. Then the
Envoy
 * bootstrap configuration can be set to defer the instantiation of those block. Note that when the
 * blocks of stats are created, they carry an extra 60~100 byte overhead (depending on worker thread
 * count) due to internal bookkeeping data structures. The overhead when deferred stats are disabled
 * is just 8 bytes.
* See more context: https://github.com/envoyproxy/envoy/issues/23575
 */
template <typename StatsStructType> class DeferredCreationCompatibleInterface {
public:
  /**
   * Returns an already existing StatsStructType, or creates a new one and returns it.
   *
   * @return The new or already existing StatsStructType.
   */
  virtual StatsStructType& getOrCreate() PURE;

  /**
   * Returns whether or not the underlying stats have been initialized yet.
   *
   * @return If the underlying stats have been initialized.
   */
  virtual bool isPresent() const PURE;

  virtual ~DeferredCreationCompatibleInterface() = default;
};

// A helper class for a lazy compatible stats struct type.
template <typename StatsStructType> class DeferredCreationCompatibleStats {
public:
  explicit DeferredCreationCompatibleStats(
      std::unique_ptr<DeferredCreationCompatibleInterface<StatsStructType>> d)
      : data_(std::move(d)) {}
  // Allows move construct and assign.
  DeferredCreationCompatibleStats& operator=(DeferredCreationCompatibleStats&&) noexcept = default;
  DeferredCreationCompatibleStats(DeferredCreationCompatibleStats&&) noexcept = default;

  inline StatsStructType* operator->() { return &data_->getOrCreate(); };
  inline StatsStructType& operator*() { return data_->getOrCreate(); };

  /**
   * Returns whether or not the underlying data_ has been initialized yet.
   *
   * @return If the underlying data_ has been initialized.
   */
  bool isPresent() const { return data_->isPresent(); }

private:
  std::unique_ptr<DeferredCreationCompatibleInterface<StatsStructType>> data_;
};
} // namespace Stats
} // namespace Envoy
