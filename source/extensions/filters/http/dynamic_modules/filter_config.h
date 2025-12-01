#pragma once

#include "envoy/server/factory_context.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/extensions/dynamic_modules/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

// The custom stat namespace which prepends all the user-defined metrics.
// Note that the prefix is removed from the final output of /stats endpoints.
constexpr absl::string_view CustomStatNamespace = "dynamicmodulescustom";

using OnHttpConfigDestroyType = decltype(&envoy_dynamic_module_on_http_filter_config_destroy);
using OnHttpFilterNewType = decltype(&envoy_dynamic_module_on_http_filter_new);

using OnHttpPerRouteConfigDestroyType =
    decltype(&envoy_dynamic_module_on_http_filter_per_route_config_destroy);
using OnHttpFilterRequestHeadersType =
    decltype(&envoy_dynamic_module_on_http_filter_request_headers);
using OnHttpFilterRequestBodyType = decltype(&envoy_dynamic_module_on_http_filter_request_body);
using OnHttpFilterRequestTrailersType =
    decltype(&envoy_dynamic_module_on_http_filter_request_trailers);
using OnHttpFilterResponseHeadersType =
    decltype(&envoy_dynamic_module_on_http_filter_response_headers);
using OnHttpFilterResponseBodyType = decltype(&envoy_dynamic_module_on_http_filter_response_body);
using OnHttpFilterResponseTrailersType =
    decltype(&envoy_dynamic_module_on_http_filter_response_trailers);
using OnHttpFilterStreamCompleteType =
    decltype(&envoy_dynamic_module_on_http_filter_stream_complete);
using OnHttpFilterDestroyType = decltype(&envoy_dynamic_module_on_http_filter_destroy);
using OnHttpFilterHttpCalloutDoneType =
    decltype(&envoy_dynamic_module_on_http_filter_http_callout_done);
using OnHttpFilterScheduled = decltype(&envoy_dynamic_module_on_http_filter_scheduled);
using OnHttpFilterDownstreamAboveWriteBufferHighWatermark =
    decltype(&envoy_dynamic_module_on_http_filter_downstream_above_write_buffer_high_watermark);
using OnHttpFilterDownstreamBelowWriteBufferLowWatermark =
    decltype(&envoy_dynamic_module_on_http_filter_downstream_below_write_buffer_low_watermark);

/**
 * A config to create http filters based on a dynamic module. This will be owned by multiple
 * filter instances. This resolves and holds the symbols used for the HTTP filters.
 * Each filter instance and the factory callback holds a shared pointer to this config.
 */
class DynamicModuleHttpFilterConfig {
public:
  /**
   * Constructor for the config.
   * @param filter_name the name of the filter.
   * @param filter_config the configuration for the module.
   * @param dynamic_module the dynamic module to use.
   * @param context the server factory context.
   */
  DynamicModuleHttpFilterConfig(const absl::string_view filter_name,
                                const absl::string_view filter_config,
                                DynamicModulePtr dynamic_module, Stats::Scope& stats_scope,
                                Server::Configuration::ServerFactoryContext& context);

  ~DynamicModuleHttpFilterConfig();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_http_filter_config_module_ptr in_module_config_ = nullptr;

  // The function pointers for the module related to the HTTP filter. All of them are resolved
  // during the construction of the config and made sure they are not nullptr after that.

  OnHttpConfigDestroyType on_http_filter_config_destroy_ = nullptr;
  OnHttpFilterNewType on_http_filter_new_ = nullptr;
  OnHttpFilterRequestHeadersType on_http_filter_request_headers_ = nullptr;
  OnHttpFilterRequestBodyType on_http_filter_request_body_ = nullptr;
  OnHttpFilterRequestTrailersType on_http_filter_request_trailers_ = nullptr;
  OnHttpFilterResponseHeadersType on_http_filter_response_headers_ = nullptr;
  OnHttpFilterResponseBodyType on_http_filter_response_body_ = nullptr;
  OnHttpFilterResponseTrailersType on_http_filter_response_trailers_ = nullptr;
  OnHttpFilterStreamCompleteType on_http_filter_stream_complete_ = nullptr;
  OnHttpFilterDestroyType on_http_filter_destroy_ = nullptr;
  OnHttpFilterHttpCalloutDoneType on_http_filter_http_callout_done_ = nullptr;
  OnHttpFilterScheduled on_http_filter_scheduled_ = nullptr;
  OnHttpFilterDownstreamAboveWriteBufferHighWatermark
      on_http_filter_downstream_above_write_buffer_high_watermark_ = nullptr;
  OnHttpFilterDownstreamBelowWriteBufferLowWatermark
      on_http_filter_downstream_below_write_buffer_low_watermark_ = nullptr;

  Envoy::Upstream::ClusterManager& cluster_manager_;
  const Stats::ScopeSharedPtr stats_scope_;
  Stats::StatNamePool stat_name_pool_;
  // We only allow the module to create stats during envoy_dynamic_module_on_http_filter_config_new,
  // and not later during request handling, so that we don't have to wrap the stat storage in a
  // lock.
  bool stat_creation_frozen_ = false;

  bool terminal_filter_ = false;

  class ModuleCounterHandle {
  public:
    ModuleCounterHandle(Stats::Counter& counter) : counter_(counter) {}

    void add(uint64_t amount) const { counter_.add(amount); }

  private:
    Stats::Counter& counter_;
  };

  class ModuleCounterVecHandle {
  public:
    ModuleCounterVecHandle(Stats::StatName name, Stats::StatNameVec label_names)
        : name_(name), label_names_(label_names) {}

    const Stats::StatNameVec& getLabelNames() const { return label_names_; }
    void add(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::counterFromElements(scope, {name_}, tags).add(amount);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
  };

  class ModuleGaugeHandle {
  public:
    ModuleGaugeHandle(Stats::Gauge& gauge) : gauge_(gauge) {}

    void increase(uint64_t amount) const { gauge_.add(amount); }
    void decrease(uint64_t amount) const { gauge_.sub(amount); }
    void set(uint64_t amount) const { gauge_.set(amount); }

  private:
    Stats::Gauge& gauge_;
  };

  class ModuleGaugeVecHandle {
  public:
    ModuleGaugeVecHandle(Stats::StatName name, Stats::StatNameVec label_names,
                         Stats::Gauge::ImportMode import_mode)
        : name_(name), label_names_(label_names), import_mode_(import_mode) {}

    const Stats::StatNameVec& getLabelNames() const { return label_names_; }

    void increase(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                  uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).add(amount);
    }
    void decrease(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                  uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).sub(amount);
    }
    void set(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).set(amount);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
    Stats::Gauge::ImportMode import_mode_;
  };

  class ModuleHistogramHandle {
  public:
    ModuleHistogramHandle(Stats::Histogram& histogram) : histogram_(histogram) {}

    void recordValue(uint64_t value) const { histogram_.recordValue(value); }

  private:
    Stats::Histogram& histogram_;
  };

  class ModuleHistogramVecHandle {
  public:
    ModuleHistogramVecHandle(Stats::StatName name, Stats::StatNameVec label_names,
                             Stats::Histogram::Unit unit)
        : name_(name), label_names_(label_names), unit_(unit) {}

    const Stats::StatNameVec& getLabelNames() const { return label_names_; }

    void recordValue(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                     uint64_t value) const {
      ASSERT(tags.has_value());
      Stats::Utility::histogramFromElements(scope, {name_}, unit_, tags).recordValue(value);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
    Stats::Histogram::Unit unit_;
  };

  size_t addCounter(ModuleCounterHandle&& counter) {
    size_t id = counters_.size();
    counters_.push_back(std::move(counter));
    return id;
  }

  size_t addCounterVec(ModuleCounterVecHandle&& counter_vec) {
    size_t id = counter_vecs_.size();
    counter_vecs_.push_back(std::move(counter_vec));
    return id;
  }

  OptRef<const ModuleCounterHandle> getCounterById(size_t id) const {
    if (id >= counters_.size()) {
      return {};
    }
    return counters_[id];
  }

  OptRef<const ModuleCounterVecHandle> getCounterVecById(size_t id) const {
    if (id >= counter_vecs_.size()) {
      return {};
    }
    return counter_vecs_[id];
  }

  size_t addGauge(ModuleGaugeHandle&& gauge) {
    size_t id = gauges_.size();
    gauges_.push_back(std::move(gauge));
    return id;
  }

  size_t addGaugeVec(ModuleGaugeVecHandle&& gauge_vec) {
    size_t id = gauge_vecs_.size();
    gauge_vecs_.push_back(std::move(gauge_vec));
    return id;
  }

  OptRef<const ModuleGaugeHandle> getGaugeById(size_t id) const {
    if (id >= gauges_.size()) {
      return {};
    }
    return gauges_[id];
  }

  OptRef<const ModuleGaugeVecHandle> getGaugeVecById(size_t id) const {
    if (id >= gauge_vecs_.size()) {
      return {};
    }
    return gauge_vecs_[id];
  }

  size_t addHistogram(ModuleHistogramHandle&& hist) {
    size_t id = hists_.size();
    hists_.push_back(std::move(hist));
    return id;
  }

  OptRef<const ModuleHistogramHandle> getHistogramById(size_t id) const {
    if (id >= hists_.size()) {
      return {};
    }
    return hists_[id];
  }

  size_t addHistogramVec(ModuleHistogramVecHandle&& hist_vec) {
    size_t id = hist_vecs_.size();
    hist_vecs_.push_back(std::move(hist_vec));
    return id;
  }

  OptRef<const ModuleHistogramVecHandle> getHistogramVecById(size_t id) const {
    if (id >= hist_vecs_.size()) {
      return {};
    }
    return hist_vecs_[id];
  }

private:
  // The name of the filter passed in the constructor.
  const std::string filter_name_;

  // The configuration for the module.
  const std::string filter_config_;

  // The cached references to stats and their metadata.
  std::vector<ModuleCounterHandle> counters_;
  std::vector<ModuleCounterVecHandle> counter_vecs_;
  std::vector<ModuleGaugeHandle> gauges_;
  std::vector<ModuleGaugeVecHandle> gauge_vecs_;
  std::vector<ModuleHistogramHandle> hists_;
  std::vector<ModuleHistogramVecHandle> hist_vecs_;

  // The handle for the module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

class DynamicModuleHttpPerRouteFilterConfig : public Router::RouteSpecificFilterConfig {
public:
  DynamicModuleHttpPerRouteFilterConfig(
      envoy_dynamic_module_type_http_filter_config_module_ptr config,
      OnHttpPerRouteConfigDestroyType destroy,
      Extensions::DynamicModules::DynamicModulePtr dynamic_module)
      : config_(config), destroy_(destroy), dynamic_module_(std::move(dynamic_module)) {}
  ~DynamicModuleHttpPerRouteFilterConfig() override;

  envoy_dynamic_module_type_http_filter_config_module_ptr config_;

private:
  OnHttpPerRouteConfigDestroyType destroy_;
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

using DynamicModuleHttpFilterConfigSharedPtr = std::shared_ptr<DynamicModuleHttpFilterConfig>;
using DynamicModuleHttpPerRouteFilterConfigConstSharedPtr =
    std::shared_ptr<const DynamicModuleHttpPerRouteFilterConfig>;

absl::StatusOr<DynamicModuleHttpPerRouteFilterConfigConstSharedPtr>
newDynamicModuleHttpPerRouteConfig(const absl::string_view per_route_config_name,
                                   const absl::string_view filter_config,
                                   Extensions::DynamicModules::DynamicModulePtr dynamic_module);

/**
 * Creates a new DynamicModuleHttpFilterConfig for given configuration.
 * @param filter_name the name of the filter.
 * @param filter_config the configuration for the module.
 * @param dynamic_module the dynamic module to use.
 * @param context the server factory context.
 * @return a shared pointer to the new config object or an error if the module could not be loaded.
 */
absl::StatusOr<DynamicModuleHttpFilterConfigSharedPtr> newDynamicModuleHttpFilterConfig(
    const absl::string_view filter_name, const absl::string_view filter_config,
    const bool terminal_filter, Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Stats::Scope& stats_scope, Server::Configuration::ServerFactoryContext& context);

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
