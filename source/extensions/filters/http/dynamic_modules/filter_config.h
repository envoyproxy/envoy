#pragma once

#include "envoy/server/factory_context.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace HttpFilters {

// The default custom stat namespace which prepends all user-defined metrics.
// Note that the prefix is removed from the final output of ``/stats`` endpoints.
// This can be overridden via the ``metrics_namespace`` field in ``DynamicModuleConfig``.
constexpr absl::string_view DefaultMetricsNamespace = "dynamicmodulescustom";

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
using OnHttpFilterHttpStreamHeadersType =
    decltype(&envoy_dynamic_module_on_http_filter_http_stream_headers);
using OnHttpFilterHttpStreamDataType =
    decltype(&envoy_dynamic_module_on_http_filter_http_stream_data);
using OnHttpFilterHttpStreamTrailersType =
    decltype(&envoy_dynamic_module_on_http_filter_http_stream_trailers);
using OnHttpFilterHttpStreamCompleteType =
    decltype(&envoy_dynamic_module_on_http_filter_http_stream_complete);
using OnHttpFilterHttpStreamResetType =
    decltype(&envoy_dynamic_module_on_http_filter_http_stream_reset);
using OnHttpFilterScheduled = decltype(&envoy_dynamic_module_on_http_filter_scheduled);
using OnHttpFilterDownstreamAboveWriteBufferHighWatermark =
    decltype(&envoy_dynamic_module_on_http_filter_downstream_above_write_buffer_high_watermark);
using OnHttpFilterDownstreamBelowWriteBufferLowWatermark =
    decltype(&envoy_dynamic_module_on_http_filter_downstream_below_write_buffer_low_watermark);
using OnHttpFilterLocalReplyType = decltype(&envoy_dynamic_module_on_http_filter_local_reply);
using OnHttpFilterConfigScheduled = decltype(&envoy_dynamic_module_on_http_filter_config_scheduled);
using OnHttpFilterConfigHttpCalloutDoneType =
    decltype(&envoy_dynamic_module_on_http_filter_config_http_callout_done);
using OnHttpFilterConfigHttpStreamHeadersType =
    decltype(&envoy_dynamic_module_on_http_filter_config_http_stream_headers);
using OnHttpFilterConfigHttpStreamDataType =
    decltype(&envoy_dynamic_module_on_http_filter_config_http_stream_data);
using OnHttpFilterConfigHttpStreamTrailersType =
    decltype(&envoy_dynamic_module_on_http_filter_config_http_stream_trailers);
using OnHttpFilterConfigHttpStreamCompleteType =
    decltype(&envoy_dynamic_module_on_http_filter_config_http_stream_complete);
using OnHttpFilterConfigHttpStreamResetType =
    decltype(&envoy_dynamic_module_on_http_filter_config_http_stream_reset);

/**
 * A config to create http filters based on a dynamic module. This will be owned by multiple
 * filter instances. This resolves and holds the symbols used for the HTTP filters.
 * Each filter instance and the factory callback holds a shared pointer to this config.
 */
class DynamicModuleHttpFilterConfig
    : public std::enable_shared_from_this<DynamicModuleHttpFilterConfig> {
public:
  /**
   * Constructor for the config.
   * @param filter_name the name of the filter.
   * @param filter_config the configuration for the module.
   * @param metrics_namespace the namespace prefix for metrics.
   * @param dynamic_module the dynamic module to use.
   * @param stats_scope the stats scope for metric creation.
   * @param context the server factory context.
   */
  DynamicModuleHttpFilterConfig(const absl::string_view filter_name,
                                const absl::string_view filter_config,
                                const absl::string_view metrics_namespace,
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
  OnHttpFilterHttpStreamHeadersType on_http_filter_http_stream_headers_ = nullptr;
  OnHttpFilterHttpStreamDataType on_http_filter_http_stream_data_ = nullptr;
  OnHttpFilterHttpStreamTrailersType on_http_filter_http_stream_trailers_ = nullptr;
  OnHttpFilterHttpStreamCompleteType on_http_filter_http_stream_complete_ = nullptr;
  OnHttpFilterHttpStreamResetType on_http_filter_http_stream_reset_ = nullptr;
  OnHttpFilterScheduled on_http_filter_scheduled_ = nullptr;
  OnHttpFilterDownstreamAboveWriteBufferHighWatermark
      on_http_filter_downstream_above_write_buffer_high_watermark_ = nullptr;
  OnHttpFilterDownstreamBelowWriteBufferLowWatermark
      on_http_filter_downstream_below_write_buffer_low_watermark_ = nullptr;
  OnHttpFilterLocalReplyType on_http_filter_local_reply_ = nullptr;
  OnHttpFilterConfigScheduled on_http_filter_config_scheduled_ = nullptr;
  OnHttpFilterConfigHttpCalloutDoneType on_http_filter_config_http_callout_done_ = nullptr;
  OnHttpFilterConfigHttpStreamHeadersType on_http_filter_config_http_stream_headers_ = nullptr;
  OnHttpFilterConfigHttpStreamDataType on_http_filter_config_http_stream_data_ = nullptr;
  OnHttpFilterConfigHttpStreamTrailersType on_http_filter_config_http_stream_trailers_ = nullptr;
  OnHttpFilterConfigHttpStreamCompleteType on_http_filter_config_http_stream_complete_ = nullptr;
  OnHttpFilterConfigHttpStreamResetType on_http_filter_config_http_stream_reset_ = nullptr;

  Envoy::Upstream::ClusterManager& cluster_manager_;
  Event::Dispatcher& main_thread_dispatcher_;
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

// We use 1-based IDs for the metrics in the ABI, so we need to convert them to 0-based indices
// for our internal storage. These helper functions do that conversion.
#define ID_TO_INDEX(id) ((id) - 1)

  size_t addCounter(ModuleCounterHandle&& counter) {
    counters_.push_back(std::move(counter));
    return counters_.size();
  }

  size_t addCounterVec(ModuleCounterVecHandle&& counter_vec) {
    counter_vecs_.push_back(std::move(counter_vec));
    return counter_vecs_.size();
  }

  OptRef<const ModuleCounterHandle> getCounterById(size_t id) const {
    if (id == 0 || id > counters_.size()) {
      return {};
    }
    return counters_[ID_TO_INDEX(id)];
  }

  OptRef<const ModuleCounterVecHandle> getCounterVecById(size_t id) const {
    if (id == 0 || id > counter_vecs_.size()) {
      return {};
    }
    return counter_vecs_[ID_TO_INDEX(id)];
  }

  size_t addGauge(ModuleGaugeHandle&& gauge) {
    gauges_.push_back(std::move(gauge));
    return gauges_.size();
  }

  size_t addGaugeVec(ModuleGaugeVecHandle&& gauge_vec) {
    gauge_vecs_.push_back(std::move(gauge_vec));
    return gauge_vecs_.size();
  }

  OptRef<const ModuleGaugeHandle> getGaugeById(size_t id) const {
    if (id == 0 || id > gauges_.size()) {
      return {};
    }
    return gauges_[ID_TO_INDEX(id)];
  }

  OptRef<const ModuleGaugeVecHandle> getGaugeVecById(size_t id) const {
    if (id == 0 || id > gauge_vecs_.size()) {
      return {};
    }
    return gauge_vecs_[ID_TO_INDEX(id)];
  }

  size_t addHistogram(ModuleHistogramHandle&& hist) {
    hists_.push_back(std::move(hist));
    return hists_.size();
  }

  size_t addHistogramVec(ModuleHistogramVecHandle&& hist_vec) {
    hist_vecs_.push_back(std::move(hist_vec));
    return hist_vecs_.size();
  }

  OptRef<const ModuleHistogramHandle> getHistogramById(size_t id) const {
    if (id == 0 || id > hists_.size()) {
      return {};
    }
    return hists_[ID_TO_INDEX(id)];
  }

  OptRef<const ModuleHistogramVecHandle> getHistogramVecById(size_t id) const {
    if (id == 0 || id > hist_vecs_.size()) {
      return {};
    }
    return hist_vecs_[ID_TO_INDEX(id)];
  }

#undef ID_TO_INDEX

private:
  // The name of the filter passed in the constructor.
  const std::string filter_name_;

  // The configuration for the module.
  const std::string filter_config_;

  // The namespace prefix for metrics.
  const std::string metrics_namespace_;

  // The cached references to stats and their metadata.
  std::vector<ModuleCounterHandle> counters_;
  std::vector<ModuleCounterVecHandle> counter_vecs_;
  std::vector<ModuleGaugeHandle> gauges_;
  std::vector<ModuleGaugeVecHandle> gauge_vecs_;
  std::vector<ModuleHistogramHandle> hists_;
  std::vector<ModuleHistogramVecHandle> hist_vecs_;

  // The handle for the module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;

public:
  /**
   * This is called when an event is scheduled via DynamicModuleHttpFilterConfigScheduler::commit.
   */
  void onScheduled(uint64_t event_id);

  /**
   * Sends an HTTP callout to the specified cluster with the given message.
   */
  envoy_dynamic_module_type_http_callout_init_result
  sendHttpCallout(uint64_t* callout_id_out, absl::string_view cluster_name,
                  Http::RequestMessagePtr&& message, uint64_t timeout_milliseconds);

  /**
   * Starts a streamable HTTP callout to the specified cluster with the given message.
   */
  envoy_dynamic_module_type_http_callout_init_result
  startHttpStream(uint64_t* stream_id_out, absl::string_view cluster_name,
                  Http::RequestMessagePtr&& message, bool end_stream,
                  uint64_t timeout_milliseconds);

  /**
   * Resets an ongoing streamable HTTP callout stream.
   */
  void resetHttpStream(uint64_t stream_id);

  /**
   * Sends data on an ongoing streamable HTTP callout stream.
   */
  bool sendStreamData(uint64_t stream_id, Buffer::Instance& data, bool end_stream);

  /**
   * Sends trailers on an ongoing streamable HTTP callout stream.
   */
  bool sendStreamTrailers(uint64_t stream_id, Http::RequestTrailerMapPtr trailers);

private:
  /**
   * Callback for one-shot HTTP callouts initiated from an HTTP filter config.
   */
  class HttpCalloutCallback : public Http::AsyncClient::Callbacks {
  public:
    HttpCalloutCallback(DynamicModuleHttpFilterConfig& config, uint64_t id)
        : config_(config), callout_id_(id) {}
    ~HttpCalloutCallback() override = default;

    void onSuccess(const Http::AsyncClient::Request& request,
                   Http::ResponseMessagePtr&& response) override;
    void onFailure(const Http::AsyncClient::Request& request,
                   Http::AsyncClient::FailureReason reason) override;
    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                      const Http::ResponseHeaderMap*) override {};
    // Used to cancel the callout if the config is destroyed before completion.
    Http::AsyncClient::Request* request_ = nullptr;

  private:
    DynamicModuleHttpFilterConfig& config_;
    const uint64_t callout_id_{};
  };

  /**
   * Callback for streaming HTTP callouts initiated from an HTTP filter config.
   */
  class HttpStreamCalloutCallback : public Http::AsyncClient::StreamCallbacks,
                                    public Event::DeferredDeletable {
  public:
    HttpStreamCalloutCallback(DynamicModuleHttpFilterConfig& config, uint64_t callout_id)
        : callout_id_(callout_id), config_(config) {}
    ~HttpStreamCalloutCallback() override = default;

    void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
    void onData(Buffer::Instance& data, bool end_stream) override;
    void onTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
    void onComplete() override;
    void onReset() override;

    Http::AsyncClient::Stream* stream_ = nullptr;
    Http::RequestMessagePtr request_message_ = nullptr;
    Http::RequestTrailerMapPtr request_trailers_ = nullptr;
    const uint64_t callout_id_{};
    bool cleaned_up_ = false;

  private:
    DynamicModuleHttpFilterConfig& config_;
  };

  /**
   * This is a helper function to get the `this` pointer as a void pointer which is passed to the
   * various event hooks.
   */
  void* thisAsVoidPtr() { return static_cast<void*>(this); }

  uint64_t getNextCalloutId() { return next_callout_id_++; }

  uint64_t next_callout_id_ = 1; // 0 is reserved as an invalid id.

  absl::flat_hash_map<uint64_t, std::unique_ptr<DynamicModuleHttpFilterConfig::HttpCalloutCallback>>
      http_callouts_;
  absl::flat_hash_map<uint64_t,
                      std::unique_ptr<DynamicModuleHttpFilterConfig::HttpStreamCalloutCallback>>
      http_stream_callouts_;
};

class DynamicModuleHttpFilterConfigScheduler {
public:
  DynamicModuleHttpFilterConfigScheduler(std::weak_ptr<DynamicModuleHttpFilterConfig> config,
                                         Event::Dispatcher& dispatcher)
      : config_(std::move(config)), dispatcher_(dispatcher) {}

  void commit(uint64_t event_id) {
    dispatcher_.post([config = config_, event_id]() {
      if (std::shared_ptr<DynamicModuleHttpFilterConfig> config_shared = config.lock()) {
        config_shared->onScheduled(event_id);
      }
    });
  }

private:
  std::weak_ptr<DynamicModuleHttpFilterConfig> config_;
  Event::Dispatcher& dispatcher_;
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
newDynamicModuleHttpPerRouteConfig(const absl::string_view filter_name,
                                   const absl::string_view filter_config,
                                   Extensions::DynamicModules::DynamicModulePtr dynamic_module);

/**
 * Creates a new DynamicModuleHttpFilterConfig for given configuration.
 * @param filter_name the name of the filter.
 * @param filter_config the configuration for the module.
 * @param metrics_namespace the namespace prefix for metrics emitted by this module.
 * @param terminal_filter whether the filter is terminal.
 * @param dynamic_module the dynamic module to use.
 * @param stats_scope the stats scope for metric creation.
 * @param context the server factory context.
 * @return a shared pointer to the new config object or an error if the module could not be loaded.
 */
absl::StatusOr<DynamicModuleHttpFilterConfigSharedPtr> newDynamicModuleHttpFilterConfig(
    const absl::string_view filter_name, const absl::string_view filter_config,
    const absl::string_view metrics_namespace, const bool terminal_filter,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope,
    Server::Configuration::ServerFactoryContext& context);

} // namespace HttpFilters
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
