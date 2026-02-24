#pragma once

#include <string>

#include "envoy/common/optref.h"
#include "envoy/event/dispatcher.h"
#include "envoy/http/async_client.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/store.h"

#include "source/common/common/logger.h"
#include "source/common/http/message_impl.h"
#include "source/common/init/target_impl.h"
#include "source/common/stats/utility.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

using OnBootstrapExtensionConfigDestroyType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_config_destroy);
using OnBootstrapExtensionNewType = decltype(&envoy_dynamic_module_on_bootstrap_extension_new);
using OnBootstrapExtensionServerInitializedType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_server_initialized);
using OnBootstrapExtensionWorkerThreadInitializedType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized);
using OnBootstrapExtensionDestroyType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_destroy);
using OnBootstrapExtensionDrainStartedType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_drain_started);
using OnBootstrapExtensionShutdownType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_shutdown);
using OnBootstrapExtensionConfigScheduledType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_config_scheduled);
using OnBootstrapExtensionHttpCalloutDoneType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_http_callout_done);
using OnBootstrapExtensionTimerFiredType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_timer_fired);
using OnBootstrapExtensionAdminRequestType =
    decltype(&envoy_dynamic_module_on_bootstrap_extension_admin_request);

class DynamicModuleBootstrapExtension;

/**
 * A config to create bootstrap extensions based on a dynamic module. This will be owned by the
 * bootstrap extension. This resolves and holds the symbols used for the bootstrap extension.
 */
class DynamicModuleBootstrapExtensionConfig
    : public std::enable_shared_from_this<DynamicModuleBootstrapExtensionConfig>,
      public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  /**
   * Constructor for the config.
   * @param extension_name the name of the extension.
   * @param extension_config the configuration for the module.
   * @param dynamic_module the dynamic module to use.
   * @param main_thread_dispatcher the main thread dispatcher.
   * @param context the server factory context for accessing cluster manager lazily.
   * @param stats_store the stats store for accessing metrics.
   */
  DynamicModuleBootstrapExtensionConfig(const absl::string_view extension_name,
                                        const absl::string_view extension_config,
                                        Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                                        Event::Dispatcher& main_thread_dispatcher,
                                        Server::Configuration::ServerFactoryContext& context,
                                        Stats::Store& stats_store);

  ~DynamicModuleBootstrapExtensionConfig();

  /**
   * This is called when an event is scheduled via
   * DynamicModuleBootstrapExtensionConfigScheduler::commit.
   */
  void onScheduled(uint64_t event_id);

  /**
   * Helper to get the `this` pointer as a void pointer.
   */
  void* thisAsVoidPtr() { return static_cast<void*>(this); }

  /**
   * Sends an HTTP callout to the specified cluster with the given message.
   * This must be called on the main thread.
   *
   * @param callout_id_out is a pointer to a variable where the callout ID will be stored.
   * @param cluster_name is the name of the cluster to which the callout is sent.
   * @param message is the HTTP request message to send.
   * @param timeout_milliseconds is the timeout for the callout in milliseconds.
   * @return the result of the callout initialization.
   */
  envoy_dynamic_module_type_http_callout_init_result
  sendHttpCallout(uint64_t* callout_id_out, absl::string_view cluster_name,
                  Http::RequestMessagePtr&& message, uint64_t timeout_milliseconds);

  /**
   * Signals that the module's initialization is complete. This unblocks the init manager and
   * allows Envoy to start accepting traffic. An init target is automatically registered for every
   * bootstrap extension, so the module must call this exactly once to unblock startup.
   */
  void signalInitComplete();

  // The corresponding in-module configuration.
  envoy_dynamic_module_type_bootstrap_extension_config_module_ptr in_module_config_ = nullptr;

  // The function pointers for the module related to the bootstrap extension. All of them are
  // resolved during the construction of the config and made sure they are not nullptr after that.

  OnBootstrapExtensionConfigDestroyType on_bootstrap_extension_config_destroy_ = nullptr;
  OnBootstrapExtensionNewType on_bootstrap_extension_new_ = nullptr;
  OnBootstrapExtensionServerInitializedType on_bootstrap_extension_server_initialized_ = nullptr;
  OnBootstrapExtensionWorkerThreadInitializedType
      on_bootstrap_extension_worker_thread_initialized_ = nullptr;
  OnBootstrapExtensionDestroyType on_bootstrap_extension_destroy_ = nullptr;
  OnBootstrapExtensionDrainStartedType on_bootstrap_extension_drain_started_ = nullptr;
  OnBootstrapExtensionShutdownType on_bootstrap_extension_shutdown_ = nullptr;
  OnBootstrapExtensionConfigScheduledType on_bootstrap_extension_config_scheduled_ = nullptr;
  OnBootstrapExtensionHttpCalloutDoneType on_bootstrap_extension_http_callout_done_ = nullptr;
  OnBootstrapExtensionTimerFiredType on_bootstrap_extension_timer_fired_ = nullptr;
  OnBootstrapExtensionAdminRequestType on_bootstrap_extension_admin_request_ = nullptr;

  // The dynamic module.
  Extensions::DynamicModules::DynamicModulePtr dynamic_module_;

  // The main thread dispatcher.
  Event::Dispatcher& main_thread_dispatcher_;

  // The server factory context for accessing cluster manager lazily. ClusterManager is not
  // available during bootstrap extension creation, so we store the context and access it when
  // needed.
  Server::Configuration::ServerFactoryContext& context_;

  // The stats store for accessing metrics.
  Stats::Store& stats_store_;

  // The init target for blocking Envoy startup until the module signals readiness.
  // Created during config construction and registered with the init manager.
  std::unique_ptr<Init::TargetImpl> init_target_;

  // ----------------------------- Metrics Support -----------------------------
  // Handle classes for storing defined metrics. These follow the same pattern as the HTTP
  // filter config metrics support.

  class ModuleCounterHandle {
  public:
    ModuleCounterHandle(Stats::Counter& counter) : counter_(counter) {}
    void add(uint64_t value) const { counter_.add(value); }

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
    void add(uint64_t value) const { gauge_.add(value); }
    void sub(uint64_t value) const { gauge_.sub(value); }
    void set(uint64_t value) const { gauge_.set(value); }

  private:
    Stats::Gauge& gauge_;
  };

  class ModuleGaugeVecHandle {
  public:
    ModuleGaugeVecHandle(Stats::StatName name, Stats::StatNameVec label_names,
                         Stats::Gauge::ImportMode import_mode)
        : name_(name), label_names_(label_names), import_mode_(import_mode) {}

    const Stats::StatNameVec& getLabelNames() const { return label_names_; }

    void add(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).add(amount);
    }
    void sub(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
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

  size_t addHistogram(ModuleHistogramHandle&& histogram) {
    size_t id = histograms_.size();
    histograms_.push_back(std::move(histogram));
    return id;
  }

  size_t addHistogramVec(ModuleHistogramVecHandle&& histogram_vec) {
    size_t id = histogram_vecs_.size();
    histogram_vecs_.push_back(std::move(histogram_vec));
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

  OptRef<const ModuleHistogramHandle> getHistogramById(size_t id) const {
    if (id >= histograms_.size()) {
      return {};
    }
    return histograms_[id];
  }

  OptRef<const ModuleHistogramVecHandle> getHistogramVecById(size_t id) const {
    if (id >= histogram_vecs_.size()) {
      return {};
    }
    return histogram_vecs_[id];
  }

  // Stats scope for metric creation.
  const Stats::ScopeSharedPtr stats_scope_;
  Stats::StatNamePool stat_name_pool_;

  // Temporary storage for the admin response body. Set by the
  // envoy_dynamic_module_callback_bootstrap_extension_admin_set_response callback during
  // on_bootstrap_extension_admin_request, then consumed by the admin handler lambda.
  std::string admin_response_body_;

private:
  /**
   * This implementation of the AsyncClient::Callbacks is used to handle the response from the HTTP
   * callout from the parent bootstrap extension config.
   */
  class HttpCalloutCallback : public Http::AsyncClient::Callbacks {
  public:
    HttpCalloutCallback(std::shared_ptr<DynamicModuleBootstrapExtensionConfig> config, uint64_t id)
        : config_(std::move(config)), callout_id_(id) {}
    ~HttpCalloutCallback() override = default;

    void onSuccess(const Http::AsyncClient::Request& request,
                   Http::ResponseMessagePtr&& response) override;
    void onFailure(const Http::AsyncClient::Request& request,
                   Http::AsyncClient::FailureReason reason) override;
    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                      const Http::ResponseHeaderMap*) override {};

    // This is the request object that is used to send the HTTP callout. It is used to cancel the
    // callout if the config is destroyed before the callout is completed.
    Http::AsyncClient::Request* request_ = nullptr;

  private:
    const std::shared_ptr<DynamicModuleBootstrapExtensionConfig> config_;
    const uint64_t callout_id_{};
  };

  uint64_t getNextCalloutId() { return next_callout_id_++; }

  uint64_t next_callout_id_ = 1; // 0 is reserved as an invalid id.

  absl::flat_hash_map<uint64_t,
                      std::unique_ptr<DynamicModuleBootstrapExtensionConfig::HttpCalloutCallback>>
      http_callouts_;

  // Metric storage.
  std::vector<ModuleCounterHandle> counters_;
  std::vector<ModuleCounterVecHandle> counter_vecs_;
  std::vector<ModuleGaugeHandle> gauges_;
  std::vector<ModuleGaugeVecHandle> gauge_vecs_;
  std::vector<ModuleHistogramHandle> histograms_;
  std::vector<ModuleHistogramVecHandle> histogram_vecs_;
};

using DynamicModuleBootstrapExtensionConfigSharedPtr =
    std::shared_ptr<DynamicModuleBootstrapExtensionConfig>;

/**
 * This class is used to schedule a bootstrap extension config event hook from a different thread
 * than the main thread. This is created via
 * envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new and deleted via
 * envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete.
 */
class DynamicModuleBootstrapExtensionConfigScheduler {
public:
  DynamicModuleBootstrapExtensionConfigScheduler(
      std::weak_ptr<DynamicModuleBootstrapExtensionConfig> config, Event::Dispatcher& dispatcher)
      : config_(std::move(config)), dispatcher_(dispatcher) {}

  void commit(uint64_t event_id) {
    dispatcher_.post([config = config_, event_id]() {
      if (std::shared_ptr<DynamicModuleBootstrapExtensionConfig> config_shared = config.lock()) {
        config_shared->onScheduled(event_id);
      }
    });
  }

private:
  // The config that this scheduler is associated with. Using a weak pointer to avoid unnecessarily
  // extending the lifetime of the config.
  std::weak_ptr<DynamicModuleBootstrapExtensionConfig> config_;
  // The dispatcher is used to post the event to the main thread.
  Event::Dispatcher& dispatcher_;
};

/**
 * This class wraps an Envoy timer for use by bootstrap extension dynamic modules. It is created via
 * envoy_dynamic_module_callback_bootstrap_extension_timer_new and deleted via
 * envoy_dynamic_module_callback_bootstrap_extension_timer_delete.
 *
 * When the timer fires, it invokes the on_bootstrap_extension_timer_fired event hook on the main
 * thread if the config is still alive.
 */
class DynamicModuleBootstrapExtensionTimer {
public:
  explicit DynamicModuleBootstrapExtensionTimer(
      std::weak_ptr<DynamicModuleBootstrapExtensionConfig> config)
      : config_(std::move(config)) {}

  /**
   * Set the underlying Envoy timer. This is separated from construction to allow the timer
   * callback to capture a stable pointer to this object.
   */
  void setTimer(Event::TimerPtr timer) { timer_ = std::move(timer); }

  Event::Timer& timer() { return *timer_; }

private:
  // The config that this timer is associated with. Using a weak pointer to avoid unnecessarily
  // extending the lifetime of the config.
  std::weak_ptr<DynamicModuleBootstrapExtensionConfig> config_;
  // The underlying Envoy timer.
  Event::TimerPtr timer_;
};

/**
 * Creates a new DynamicModuleBootstrapExtensionConfig from the given module and configuration.
 * @param extension_name the name of the extension.
 * @param extension_config the configuration for the module.
 * @param dynamic_module the dynamic module to use.
 * @param main_thread_dispatcher the main thread dispatcher.
 * @param context the server factory context for accessing cluster manager lazily.
 * @param stats_store the stats store for accessing metrics.
 * @return an error status if the module could not be loaded or the configuration could not be
 * created, or a shared pointer to the config.
 */
absl::StatusOr<DynamicModuleBootstrapExtensionConfigSharedPtr>
newDynamicModuleBootstrapExtensionConfig(
    const absl::string_view extension_name, const absl::string_view extension_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Event::Dispatcher& main_thread_dispatcher, Server::Configuration::ServerFactoryContext& context,
    Stats::Store& stats_store);

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
