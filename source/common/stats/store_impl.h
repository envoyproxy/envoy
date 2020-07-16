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

  SymbolTable& symbolTable() override { return symbol_table_; }
  const SymbolTable& constSymbolTable() const override { return symbol_table_; }

  /*CounterOptConstRef slowFindCounterByString(absl::string_view name) const override;
  GaugeOptConstRef slowFindGaugeByString(absl::string_view name) const override;
  HistogramOptConstRef slowFindHistogramByString(absl::string_view name) const override;
  TextReadoutOptConstRef slowFindTextReadoutByString(absl::string_view name) const override;*/

  /*bool iterate(const CounterFn& fn) const override;
  bool iterate(const GaugeFn& fn) const override;
  bool iterate(const HistogramFn& fn) const override;
  bool iterate(const TextReadoutFn& fn) const override;*/

private:
  SymbolTable& symbol_table_;
};

} // namespace Stats
} // namespace Envoy
