#include "common/stats/isolated_store_impl.h"

#include <string.h>

#include <algorithm>
#include <string>

#include "common/common/utility.h"
#include "common/stats/fake_symbol_table_impl.h"
#include "common/stats/histogram_impl.h"
#include "common/stats/scope_prefixer.h"
#include "common/stats/utility.h"

namespace Envoy {
namespace Stats {

IsolatedStoreImpl::IsolatedStoreImpl()
    : IsolatedStoreImpl(std::make_unique<FakeSymbolTableImpl>()) {}

IsolatedStoreImpl::IsolatedStoreImpl(std::unique_ptr<SymbolTable>&& symbol_table)
    : IsolatedStoreImpl(*symbol_table) {
  symbol_table_storage_ = std::move(symbol_table);
}

IsolatedStoreImpl::IsolatedStoreImpl(SymbolTable& symbol_table)
    : StoreImpl(symbol_table), alloc_(symbol_table),
      counters_([this](const std::string& name) -> CounterSharedPtr {
        std::string tag_extracted_name = name;
        std::vector<Tag> tags;
        return alloc_.makeCounter(name, std::move(tag_extracted_name), std::move(tags));
      }),
      gauges_([this](const std::string& name) -> GaugeSharedPtr {
        std::string tag_extracted_name = name;
        std::vector<Tag> tags;
        return alloc_.makeGauge(name, std::move(tag_extracted_name), std::move(tags));
      }),
      histograms_([this](const std::string& name) -> HistogramSharedPtr {
        return std::make_shared<HistogramImpl>(name, *this, std::string(name), std::vector<Tag>());
      }) {}

ScopePtr IsolatedStoreImpl::createScope(const std::string& name) {
  return std::make_unique<ScopePrefixer>(name, *this);
}

} // namespace Stats
} // namespace Envoy
