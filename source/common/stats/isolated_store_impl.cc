#include "common/stats/isolated_store_impl.h"

#include <string.h>

#include <algorithm>
#include <string>

#include "common/common/utility.h"
#include "common/stats/histogram_impl.h"
#include "common/stats/scope_prefixer.h"
#include "common/stats/utility.h"

namespace Envoy {
namespace Stats {

IsolatedStoreImpl::IsolatedStoreImpl() : IsolatedStoreImpl(std::make_shared<SymbolTableImpl>()) {}

IsolatedStoreImpl::IsolatedStoreImpl(const SharedSymbolTable& symbol_table)
    : symbol_table_(symbol_table), alloc_(*symbol_table_),
      counters_([this](StatName name) -> CounterSharedPtr {
        return alloc_.makeCounter(name, name.toString(alloc_.symbolTable()), std::vector<Tag>());
      }),
      gauges_([this](StatName name) -> GaugeSharedPtr {
        return alloc_.makeGauge(name, name.toString(alloc_.symbolTable()), std::vector<Tag>());
      }),
      histograms_([this](StatName name) -> HistogramSharedPtr {
        return std::make_shared<HistogramImpl>(name, *this, name.toString(alloc_.symbolTable()),
                                               std::vector<Tag>());
      }) {}

ScopePtr IsolatedStoreImpl::createScope(const std::string& name) {
  return std::make_unique<ScopePrefixer>(name, *this);
}

} // namespace Stats
} // namespace Envoy
