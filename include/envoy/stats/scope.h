#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats_options.h"

namespace Envoy {
namespace Stats {

class Counter;
class Gauge;
class Histogram;
class Scope;
class StatsOptions;

typedef std::unique_ptr<Scope> ScopePtr;
typedef std::shared_ptr<Scope> ScopeSharedPtr;

/**
 * A named scope for stats. Scopes are a grouping of stats that can be acted on as a unit if needed
 * (for example to free/delete all of them).
 */
class Scope {
public:
  virtual ~Scope() {}

  /**
   * Allocate a new scope. NOTE: The implementation should correctly handle overlapping scopes
   * that point to the same reference counted backing stats. This allows a new scope to be
   * gracefully swapped in while an old scope with the same name is being destroyed.
   * @param name supplies the scope's namespace prefix.
   */
  virtual ScopePtr createScope(const std::string& name) PURE;

  /**
   * Deliver an individual histogram value to all registered sinks.
   */
  virtual void deliverHistogramToSinks(const Histogram& histogram, uint64_t value) PURE;

  /**
   * @return a counter within the scope's namespace.
   */
  virtual Counter& counter(const std::string& name) PURE;

  /**
   * @return a gauge within the scope's namespace.
   */
  virtual Gauge& gauge(const std::string& name) PURE;

  /**
   * @return a histogram within the scope's namespace with a particular value type.
   */
  virtual Histogram& histogram(const std::string& name) PURE;

  /**
   * @return a reference to the top-level StatsOptions struct, containing information about the
   * maximum allowable object name length and stat suffix length.
   */
  virtual const Stats::StatsOptions& statsOptions() const PURE;
};

} // namespace Stats
} // namespace Envoy
