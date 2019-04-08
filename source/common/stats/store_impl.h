#pragma once

#include "envoy/stats/stats.h"
#include "envoy/stats/store.h"

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Stats {

/**
 * Implements common parts of the Store API needed by multiple derivations of Store.
 */
class StoreImpl : public Store {
public:
  explicit StoreImpl(SymbolTable& symbol_table) : symbol_table_(symbol_table) {}

  Counter& counterFromStatName(StatName name) override {
    return counter(symbol_table_.toString(name));
  }

  Gauge& gaugeFromStatName(StatName name) override { return gauge(symbol_table_.toString(name)); }

  Histogram& histogramFromStatName(StatName name) override {
    return histogram(symbol_table_.toString(name));
  }

  SymbolTable& symbolTable() override { return symbol_table_; }
  const SymbolTable& symbolTable() const override { return symbol_table_; }

private:
  SymbolTable& symbol_table_;
};

} // namespace Stats
} // namespace Envoy
