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

  Envoy::Upstream::ClusterManager& cluster_manager_;
  const Stats::ScopeSharedPtr stats_scope_;
  Stats::StatNamePool stat_name_pool_;
  // We only allow the module to create stats during envoy_dynamic_module_on_http_filter_config_new,
  // and not later during request handling, so that we don't have to wrap the stat storage in a
  // lock.
  bool stat_creation_frozen_ = false;

  using StatNameVecConstOptRef = OptRef<const Stats::StatNameVec>;

  class ModuleMetricHandle {
  public:
    virtual ~ModuleMetricHandle() = default;

    virtual StatNameVecConstOptRef getLabelNames() const { return {}; };
  };

  class ModuleCounterHandle : public ModuleMetricHandle {
  public:
    ~ModuleCounterHandle() override = default;

    // Increment the counter by the given amount.
    virtual void add(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                     uint64_t amount) const PURE;
  };
  using ModuleCounterHandlePtr = std::unique_ptr<ModuleCounterHandle>;

  class ModuleCounterHandleImpl : public ModuleCounterHandle {
  public:
    ModuleCounterHandleImpl(Stats::Counter& counter) : counter_(counter) {}

    // ModuleCounterHandle
    void add(Stats::Scope&, Stats::StatNameTagVectorOptConstRef tags,
             uint64_t amount) const override {
      ASSERT(!tags.has_value());
      counter_.add(amount);
    }

  private:
    Stats::Counter& counter_;
  };

  class ModuleCounterVecHandleImpl : public ModuleCounterHandle {
  public:
    ModuleCounterVecHandleImpl(Stats::StatName name, Stats::StatNameVec label_names)
        : name_(name), label_names_(label_names) {}

    // ModuleMetricHandle
    StatNameVecConstOptRef getLabelNames() const override { return (label_names_); }

    // ModuleCounterHandle
    void add(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
             uint64_t amount) const override {
      ASSERT(tags.has_value());
      Stats::Utility::counterFromElements(scope, {name_}, tags).add(amount);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
  };

  class ModuleGaugeHandle : public ModuleMetricHandle {
  public:
    ~ModuleGaugeHandle() override = default;

    // Increase the gauge by the given amount.
    virtual void increase(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                          uint64_t amount) const PURE;

    // Decrease the gauge by the given amount.
    virtual void decrease(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                          uint64_t amount) const PURE;

    // Set the value of the gauge to the given amount.
    virtual void set(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                     uint64_t amount) const PURE;
  };
  using ModuleGaugeHandlePtr = std::unique_ptr<ModuleGaugeHandle>;

  class ModuleGaugeHandleImpl : public ModuleGaugeHandle {
  public:
    ModuleGaugeHandleImpl(Stats::Gauge& gauge) : gauge_(gauge) {}

    // ModuleGaugeHandle
    void increase(Stats::Scope&, Stats::StatNameTagVectorOptConstRef tags,
                  uint64_t amount) const override {
      ASSERT(!tags.has_value());
      gauge_.add(amount);
    }
    void decrease(Stats::Scope&, Stats::StatNameTagVectorOptConstRef tags,
                  uint64_t amount) const override {
      ASSERT(!tags.has_value());
      gauge_.sub(amount);
    }
    void set(Stats::Scope&, Stats::StatNameTagVectorOptConstRef tags,
             uint64_t amount) const override {
      ASSERT(!tags.has_value());
      gauge_.set(amount);
    }

  private:
    Stats::Gauge& gauge_;
  };

  class ModuleGaugeVecHandleImpl : public ModuleGaugeHandle {
  public:
    ModuleGaugeVecHandleImpl(Stats::StatName name, Stats::StatNameVec label_names,
                             Stats::Gauge::ImportMode import_mode)
        : name_(name), label_names_(label_names), import_mode_(import_mode) {}

    // ModuleMetricHandle
    StatNameVecConstOptRef getLabelNames() const override { return (label_names_); }

    // ModuleGaugeHandle
    void increase(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                  uint64_t amount) const override {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).add(amount);
    }
    void decrease(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                  uint64_t amount) const override {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).sub(amount);
    }
    void set(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
             uint64_t amount) const override {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).set(amount);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
    Stats::Gauge::ImportMode import_mode_;
  };

  class ModuleHistogramHandle : public ModuleMetricHandle {
  public:
    ~ModuleHistogramHandle() override = default;

    // Record the given value.
    virtual void recordValue(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                             uint64_t value) const PURE;
  };
  using ModuleHistogramHandlePtr = std::unique_ptr<ModuleHistogramHandle>;

  class ModuleHistogramHandleImpl : public ModuleHistogramHandle {
  public:
    ModuleHistogramHandleImpl(Stats::Histogram& histogram) : histogram_(histogram) {}

    // ModuleHistogramHandle
    void recordValue(Stats::Scope&, Stats::StatNameTagVectorOptConstRef tags,
                     uint64_t value) const override {
      ASSERT(!tags.has_value());
      histogram_.recordValue(value);
    }

  private:
    Stats::Histogram& histogram_;
  };

  class ModuleHistogramVecHandleImpl : public ModuleHistogramHandle {
  public:
    ModuleHistogramVecHandleImpl(Stats::StatName name, Stats::StatNameVec label_names,
                                 Stats::Histogram::Unit unit)
        : name_(name), label_names_(label_names), unit_(unit) {}

    // ModuleMetricHandle
    StatNameVecConstOptRef getLabelNames() const override { return (label_names_); }

    // ModuleHistogramHandle
    void recordValue(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                     uint64_t value) const override {
      ASSERT(tags.has_value());
      Stats::Utility::histogramFromElements(scope, {name_}, unit_, tags).recordValue(value);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
    Stats::Histogram::Unit unit_;
  };

  size_t addCounter(ModuleCounterHandlePtr&& counter) {
    size_t id = counters_.size();
    counters_.push_back(std::move(counter));
    return id;
  }

  const ModuleCounterHandle& getCounterById(size_t id) const {
    ASSERT(id < counters_.size());
    return *counters_[id];
  }

  size_t addGauge(ModuleGaugeHandlePtr&& gauge) {
    size_t id = gauges_.size();
    gauges_.push_back(std::move(gauge));
    return id;
  }

  const ModuleGaugeHandle& getGaugeById(size_t id) const {
    ASSERT(id < gauges_.size());
    return *gauges_[id];
  }

  size_t addHistogram(ModuleHistogramHandlePtr&& hist) {
    size_t id = hists_.size();
    hists_.push_back(std::move(hist));
    return id;
  }

  const ModuleHistogramHandle& getHistogramById(size_t id) const {
    ASSERT(id < hists_.size());
    return *hists_[id];
  }

private:
  // The name of the filter passed in the constructor.
  const std::string filter_name_;

  // The configuration for the module.
  const std::string filter_config_;

  // The cached references to stats and their metadata.
  std::vector<ModuleCounterHandlePtr> counters_;
  std::vector<ModuleGaugeHandlePtr> gauges_;
  std::vector<ModuleHistogramHandlePtr> hists_;

  // The handle for the module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;
};

class DynamicModuleHttpPerRouteFilterConfig : public Router::RouteSpecificFilterConfig {
public:
  DynamicModuleHttpPerRouteFilterConfig(
      envoy_dynamic_module_type_http_filter_config_module_ptr config,
      OnHttpPerRouteConfigDestroyType destroy)
      : config_(config), destroy_(destroy) {}
  ~DynamicModuleHttpPerRouteFilterConfig() override;

  envoy_dynamic_module_type_http_filter_config_module_ptr config_;

private:
  OnHttpPerRouteConfigDestroyType destroy_;
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
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope,
    Server::Configuration::ServerFactoryContext& context);

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
