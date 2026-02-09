#pragma once

#include "envoy/common/optref.h"
#include "envoy/extensions/filters/udp/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DynamicModules {

// The default custom stat namespace which prepends all user-defined metrics.
// Note that the prefix is removed from the final output of ``/stats`` endpoints.
// This can be overridden via the ``metrics_namespace`` field in ``DynamicModuleConfig``.
constexpr absl::string_view DefaultMetricsNamespace = "dynamicmodulescustom";

class DynamicModuleUdpListenerFilterConfig {
public:
  DynamicModuleUdpListenerFilterConfig(
      const envoy::extensions::filters::udp::dynamic_modules::v3::DynamicModuleUdpListenerFilter&
          config,
      Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope);

  ~DynamicModuleUdpListenerFilterConfig();

  const std::string filter_name_;
  const std::string filter_config_;
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;

  envoy_dynamic_module_type_udp_listener_filter_config_module_ptr in_module_config_{nullptr};

  decltype(envoy_dynamic_module_on_udp_listener_filter_config_new)* on_filter_config_new_{nullptr};
  decltype(envoy_dynamic_module_on_udp_listener_filter_config_destroy)* on_filter_config_destroy_{
      nullptr};
  decltype(envoy_dynamic_module_on_udp_listener_filter_new)* on_filter_new_{nullptr};
  decltype(envoy_dynamic_module_on_udp_listener_filter_on_data)* on_filter_on_data_{nullptr};
  decltype(envoy_dynamic_module_on_udp_listener_filter_destroy)* on_filter_destroy_{nullptr};

  // ----------------------------- Metrics Support -----------------------------
  // Handle classes for storing defined metrics.

  class ModuleCounterHandle {
  public:
    ModuleCounterHandle(Stats::Counter& counter) : counter_(counter) {}
    void add(uint64_t value) const { counter_.add(value); }

  private:
    Stats::Counter& counter_;
  };

  class ModuleGaugeHandle {
  public:
    ModuleGaugeHandle(Stats::Gauge& gauge) : gauge_(gauge) {}
    void add(uint64_t value) const { gauge_.add(value); }
    void sub(uint64_t value) const { gauge_.sub(value); }
    void set(uint64_t value) const { gauge_.set(value); }

  private:
    Stats::Gauge& gauge_;
  };

  class ModuleHistogramHandle {
  public:
    ModuleHistogramHandle(Stats::Histogram& histogram) : histogram_(histogram) {}
    void recordValue(uint64_t value) const { histogram_.recordValue(value); }

  private:
    Stats::Histogram& histogram_;
  };

  // Methods for adding metrics during configuration.
  size_t addCounter(ModuleCounterHandle&& counter) {
    size_t id = counters_.size();
    counters_.push_back(std::move(counter));
    return id;
  }

  size_t addGauge(ModuleGaugeHandle&& gauge) {
    size_t id = gauges_.size();
    gauges_.push_back(std::move(gauge));
    return id;
  }

  size_t addHistogram(ModuleHistogramHandle&& histogram) {
    size_t id = histograms_.size();
    histograms_.push_back(std::move(histogram));
    return id;
  }

  // Methods for getting metrics by ID.
  OptRef<const ModuleCounterHandle> getCounterById(size_t id) const {
    if (id >= counters_.size()) {
      return {};
    }
    return counters_[id];
  }

  OptRef<const ModuleGaugeHandle> getGaugeById(size_t id) const {
    if (id >= gauges_.size()) {
      return {};
    }
    return gauges_[id];
  }

  OptRef<const ModuleHistogramHandle> getHistogramById(size_t id) const {
    if (id >= histograms_.size()) {
      return {};
    }
    return histograms_[id];
  }

  // Stats scope for metric creation.
  const Stats::ScopeSharedPtr stats_scope_;
  Stats::StatNamePool stat_name_pool_;

private:
  // Metric storage.
  std::vector<ModuleCounterHandle> counters_;
  std::vector<ModuleGaugeHandle> gauges_;
  std::vector<ModuleHistogramHandle> histograms_;
};

using DynamicModuleUdpListenerFilterConfigSharedPtr =
    std::shared_ptr<DynamicModuleUdpListenerFilterConfig>;

} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
