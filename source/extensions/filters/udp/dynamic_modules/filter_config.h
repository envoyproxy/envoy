#pragma once

#include "envoy/extensions/filters/udp/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/stats/scope.h"

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/dynamic_modules/metric_registry.h"

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
  // The shared registry holding all module-defined metrics.
  Extensions::DynamicModules::MetricRegistry& metrics() { return metrics_; }

  // Owns the scope the registry references. Must precede metrics_ so it initializes first.
  const Stats::ScopeSharedPtr stats_scope_;
  // Shared metrics registry composed from stats_scope_.
  Extensions::DynamicModules::MetricRegistry metrics_;
  // We only allow the module to create stats during the in-module config_new, and not later from
  // worker threads, so that we don't have to wrap the metrics registry pool in a lock.
  bool stat_creation_frozen_ = false;
};

using DynamicModuleUdpListenerFilterConfigSharedPtr =
    std::shared_ptr<DynamicModuleUdpListenerFilterConfig>;

} // namespace DynamicModules
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
