#pragma once

#include "envoy/stats/stats.h"

#include "common/stats/metric_impl.h"

namespace Envoy {
namespace Stats {

/**
 * Null counter implementation.
 * No-ops on all calls and requires no underlying metric or data.
 */
class NullCounterImpl : public Counter, NullMetricImpl {
public:
  explicit NullCounterImpl(SymbolTable& symbol_table) : NullMetricImpl(symbol_table) {}
  ~NullCounterImpl() override {
    // MetricImpl must be explicitly cleared() before destruction, otherwise it
    // will not be able to access the SymbolTable& to free the symbols. An RAII
    // alternative would be to store the SymbolTable reference in the
    // MetricImpl, costing 8 bytes per stat.
    MetricImpl::clear();
  }

  void add(uint64_t) override {}
  void inc() override {}
  uint64_t latch() override { return 0; }
  void reset() override {}
  uint64_t value() const override { return 0; }
};

} // namespace Stats
} // namespace Envoy
