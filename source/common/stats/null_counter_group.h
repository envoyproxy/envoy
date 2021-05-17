#pragma once

#include "envoy/stats/stats.h"

#include "common/stats/metric_impl.h"

namespace Envoy {
namespace Stats {

/**
 * Null counter group implementation.
 * No-ops on all calls and requires no underlying metric or data.
 */
class NullCounterGroupImpl : public MetricImpl<CounterGroup> {
public:
  explicit NullCounterGroupImpl(SymbolTable& symbol_table)
      : MetricImpl<CounterGroup>(symbol_table), symbol_table_(symbol_table) {}
  ~NullCounterGroupImpl() override {
    // MetricImpl must be explicitly cleared() before destruction, otherwise it
    // will not be able to access the SymbolTable& to free the symbols. An RAII
    // alternative would be to store the SymbolTable reference in the
    // MetricImpl, costing 8 bytes per stat.
    MetricImpl::clear(symbol_table_);
  }

  void add(size_t /*index*/, uint64_t) override {}
  void inc(size_t /*index*/) override {}
  uint64_t latch(size_t /*index*/) override { return 0; }
  void reset(size_t /*index*/) override {}
  uint64_t value(size_t /*index*/) const override { return 0; }

  // Metric
  bool used() const override { return false; }
  SymbolTable& symbolTable() override { return symbol_table_; }

  // RefcountInterface
  void incRefCount() override { refcount_helper_.incRefCount(); }
  bool decRefCount() override { return refcount_helper_.decRefCount(); }
  uint32_t use_count() const override { return refcount_helper_.use_count(); }

private:
  RefcountHelper refcount_helper_;
  SymbolTable& symbol_table_;
};

} // namespace Stats
} // namespace Envoy
