#pragma once

#include "envoy/stats/stats.h"

#include "common/stats/metric_impl.h"

namespace Envoy {
namespace Stats {

/**
 * Null gauge implementation.
 * No-ops on all calls and requires no underlying metric or data.
 */
class NullGaugeImpl : public MetricImpl<Gauge> {
public:
  explicit NullGaugeImpl(SymbolTable& symbol_table)
      : MetricImpl<Gauge>(symbol_table), symbol_table_(symbol_table) {}
  ~NullGaugeImpl() override {
    // MetricImpl must be explicitly cleared() before destruction, otherwise it
    // will not be able to access the SymbolTable& to free the symbols. An RAII
    // alternative would be to store the SymbolTable reference in the
    // MetricImpl, costing 8 bytes per stat.
    MetricImpl::clear(symbol_table_);
  }

  void add(uint64_t) override {}
  void inc() override {}
  void dec() override {}
  void set(uint64_t) override {}
  void setParentValue(uint64_t) override {}
  void sub(uint64_t) override {}
  uint64_t value() const override { return 0; }
  ImportMode importMode() const override { return ImportMode::NeverImport; }
  void mergeImportMode(ImportMode /* import_mode */) override {}

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
