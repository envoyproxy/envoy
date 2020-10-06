#include "common/stats/isolated_store_impl.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "common/common/utility.h"
#include "common/stats/histogram_impl.h"
#include "common/stats/scope_prefixer.h"
#include "common/stats/utility.h"

namespace Envoy {
namespace Stats {

IsolatedStoreImpl::IsolatedStoreImpl() : IsolatedStoreImpl(std::make_unique<SymbolTableImpl>()) {}

IsolatedStoreImpl::IsolatedStoreImpl(std::unique_ptr<SymbolTable>&& symbol_table)
    : IsolatedStoreImpl(*symbol_table) {
  symbol_table_storage_ = std::move(symbol_table);
}

IsolatedStoreImpl::IsolatedStoreImpl(SymbolTable& symbol_table)
    : StoreImpl(symbol_table), alloc_(symbol_table),
      counters_([this](StatName name) -> CounterSharedPtr {
        return alloc_.makeCounter(name, name, StatNameTagVector{});
      }),
      gauges_([this](StatName name, Gauge::ImportMode import_mode) -> GaugeSharedPtr {
        return alloc_.makeGauge(name, name, StatNameTagVector{}, import_mode);
      }),
      histograms_([this](StatName name, Histogram::Unit unit) -> HistogramSharedPtr {
        return HistogramSharedPtr(new HistogramImpl(name, unit, *this, name, StatNameTagVector{}));
      }),
      text_readouts_([this](StatName name, TextReadout::Type) -> TextReadoutSharedPtr {
        return alloc_.makeTextReadout(name, name, StatNameTagVector{});
      }),
      null_counter_(new NullCounterImpl(symbol_table)),
      null_gauge_(new NullGaugeImpl(symbol_table)) {}

ScopePtr IsolatedStoreImpl::createScope(const std::string& name) {
  return std::make_unique<ScopePrefixer>(name, *this);
}

} // namespace Stats
} // namespace Envoy
