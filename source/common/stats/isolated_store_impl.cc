#include "common/stats/isolated_store_impl.h"

#include <string.h>

#include <algorithm>
#include <string>

#include "common/common/utility.h"
#include "common/stats/histogram_impl.h"
#include "common/stats/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Stats {

IsolatedStoreImpl::IsolatedStoreImpl()
    : counters_([this](absl::string_view name) -> CounterSharedPtr {
        return alloc_.makeCounter(name, nullptr);
      }),
      gauges_([this](absl::string_view name) -> GaugeSharedPtr {
        return alloc_.makeGauge(name, nullptr);
      }),
      histograms_([this](absl::string_view name) -> HistogramSharedPtr {
        return std::make_shared<HistogramImpl>(name, *this, nullptr);
      }) {}

struct IsolatedScopeImpl : public Scope {
  IsolatedScopeImpl(IsolatedStoreImpl& parent, const std::string& prefix)
      : parent_(parent), prefix_(Utility::sanitizeStatsName(prefix)) {}

  // Stats::Scope
  ScopePtr createScope(const std::string& name) override {
    return ScopePtr{new IsolatedScopeImpl(parent_, absl::StrCat(prefix_, name))};
  }
  void deliverHistogramToSinks(const Histogram&, uint64_t) override {}
  Counter& counter(const std::string& name) override { return parent_.counter(prefix_ + name); }
  Gauge& gauge(const std::string& name) override { return parent_.gauge(prefix_ + name); }
  Histogram& histogram(const std::string& name) override {
    return parent_.histogram(prefix_ + name);
  }
  const Stats::StatsOptions& statsOptions() const override { return parent_.statsOptions(); }

  IsolatedStoreImpl& parent_;
  const std::string prefix_;
};

ScopePtr IsolatedStoreImpl::createScope(const std::string& name) {
  return ScopePtr{new IsolatedScopeImpl(*this, name)};
}

} // namespace Stats
} // namespace Envoy
