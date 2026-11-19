#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/http/async_client.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/common/init/target_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"
#include "source/extensions/dynamic_modules/metric_registry.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {

// The default custom stat namespace which prepends all user-defined metrics.
// This can be overridden via the ``metrics_namespace`` field in ``DynamicModuleConfig``.
constexpr absl::string_view DefaultMetricsNamespace = "dynamicmodulescustom";

class DynamicModuleLbConfig;
class TypedDynamicModuleLbConfig;
using DynamicModuleLbConfigSharedPtr = std::shared_ptr<DynamicModuleLbConfig>;

/**
 * Function pointer types for the load balancer ABI functions.
 */
using OnLbConfigNewType = decltype(&envoy_dynamic_module_on_lb_config_new);
using OnLbConfigDestroyType = decltype(&envoy_dynamic_module_on_lb_config_destroy);
using OnLbNewType = decltype(&envoy_dynamic_module_on_lb_new);
using OnLbChooseHostType = decltype(&envoy_dynamic_module_on_lb_choose_host);
using OnLbOnHostMembershipUpdateType =
    decltype(&envoy_dynamic_module_on_lb_on_host_membership_update);
using OnLbDestroyType = decltype(&envoy_dynamic_module_on_lb_destroy);
using OnLbConfigHttpCalloutDoneType =
    decltype(&envoy_dynamic_module_on_lb_config_http_callout_done);
using OnLbConfigTimerType = decltype(&envoy_dynamic_module_on_lb_config_timer);
using OnLbConfigServerInitializedType =
    decltype(&envoy_dynamic_module_on_lb_config_server_initialized);

/**
 * Configuration for a dynamic module load balancer. This holds the loaded dynamic module and
 * the resolved function pointers for the ABI.
 */
class DynamicModuleLbConfig : public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  /**
   * Creates a DynamicModuleLbConfig and resolves ABI symbols.
   *
   * @param lb_policy_name the name identifying the load balancer implementation in the module.
   * @param lb_config the configuration bytes to pass to the module.
   * @param metrics_namespace the namespace prefix for metrics emitted by this module.
   * @param dynamic_module the loaded dynamic module.
   * @param stats_scope the stats scope for creating custom metrics.
   * @return a shared pointer to the config, or an error status.
   */
  static absl::StatusOr<DynamicModuleLbConfigSharedPtr>
  create(const std::string& lb_policy_name, const std::string& lb_config,
         const std::string& metrics_namespace,
         Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module,
         Stats::Scope& stats_scope);

  /**
   * Initializes the in-module configuration by invoking on_lb_config_new.
   */
  absl::Status initializeInModuleConfig();

  ~DynamicModuleLbConfig();

  // Function pointers resolved from the dynamic module.
  OnLbConfigNewType on_config_new_;
  OnLbConfigDestroyType on_config_destroy_;
  OnLbNewType on_lb_new_;
  OnLbChooseHostType on_choose_host_;
  OnLbOnHostMembershipUpdateType on_host_membership_update_;
  OnLbDestroyType on_lb_destroy_;
  // Optional. Null when the module does not implement them.
  OnLbConfigHttpCalloutDoneType on_http_callout_done_ = nullptr;
  OnLbConfigTimerType on_timer_ = nullptr;
  OnLbConfigServerInitializedType on_server_initialized_ = nullptr;

  // The in-module configuration pointer.
  envoy_dynamic_module_type_lb_config_module_ptr in_module_config_{nullptr};

  // Main-thread HTTP callout and timer support.
  void setMainThreadState(TypedDynamicModuleLbConfig* main_thread_state) {
    main_thread_state_ = main_thread_state;
  }

  envoy_dynamic_module_type_http_callout_init_result
  sendHttpCallout(uint64_t* callout_id_out, absl::string_view cluster_name,
                  Http::RequestMessagePtr&& message, uint64_t timeout_milliseconds);
  void enableTimer(uint64_t interval_milliseconds);
  void disableTimer();
  void scheduleOnServerInitialized();

  // ----------------------------- Metrics Support -----------------------------
  // The shared registry holding all module-defined metrics.
  Extensions::DynamicModules::MetricRegistry& metrics() { return metrics_; }

  // Owns the scope the registry references. Must precede metrics_ so it initializes first.
  const Stats::ScopeSharedPtr stats_scope_;
  // Shared metrics registry composed from stats_scope_.
  Extensions::DynamicModules::MetricRegistry metrics_;
  // We only allow the module to create stats during on_lb_config_new, and not later from worker
  // threads, so that we don't have to wrap the metrics registry pool in a lock.
  // Per-request label values use a stack-local Stats::StatNameDynamicPool in the increment
  // callbacks (see abi_impl.cc).
  bool stat_creation_frozen_ = false;

private:
  DynamicModuleLbConfig(const std::string& lb_policy_name, const std::string& lb_config,
                        const std::string& metrics_namespace,
                        Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module,
                        Stats::Scope& stats_scope);

  const std::string lb_policy_name_;
  const std::string lb_config_;
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;

  // Dereferenced only on the main thread, where the owner is alive.
  TypedDynamicModuleLbConfig* main_thread_state_ = nullptr;
};

/**
 * Main-thread load balancer config. Owns the timer and HTTP callouts.
 *
 * Timers and in-flight callouts remain active until this config is destroyed with its parent
 * ClusterInfo, including while existing connections drain.
 */
class TypedDynamicModuleLbConfig : public Upstream::LoadBalancerConfig,
                                   public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  /**
   * Creates a TypedDynamicModuleLbConfig and initializes the in-module configuration.
   */
  static absl::StatusOr<std::unique_ptr<TypedDynamicModuleLbConfig>>
  create(DynamicModuleLbConfigSharedPtr config,
         Server::Configuration::ServerFactoryContext& server_context);

  ~TypedDynamicModuleLbConfig() override;

  const DynamicModuleLbConfigSharedPtr& config() const { return config_; }
  envoy_dynamic_module_type_http_callout_init_result
  sendHttpCallout(uint64_t* callout_id_out, absl::string_view cluster_name,
                  Http::RequestMessagePtr&& message, uint64_t timeout_milliseconds);
  void enableTimer(uint64_t interval_milliseconds);
  void disableTimer();
  void scheduleOnServerInitialized();

private:
  TypedDynamicModuleLbConfig(DynamicModuleLbConfigSharedPtr config,
                             Server::Configuration::ServerFactoryContext& server_context);

  uint64_t getNextCalloutId() { return next_callout_id_++; }
  void armServerInitializedHook();
  struct LifetimeGate {};
  void onTimer();

  /**
   * Callback for HTTP callouts initiated from the load balancer config.
   */
  class HttpCalloutCallback : public Http::AsyncClient::Callbacks {
  public:
    HttpCalloutCallback(TypedDynamicModuleLbConfig& parent, uint64_t id,
                        Event::Dispatcher& dispatcher, std::weak_ptr<LifetimeGate> lifetime)
        : parent_(parent), callout_id_(id), dispatcher_(dispatcher),
          lifetime_(std::move(lifetime)) {}
    ~HttpCalloutCallback() override = default;

    void onSuccess(const Http::AsyncClient::Request& request,
                   Http::ResponseMessagePtr&& response) override;
    void onFailure(const Http::AsyncClient::Request& request,
                   Http::AsyncClient::FailureReason reason) override;
    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                      const Http::ResponseHeaderMap*) override {};

    Http::AsyncClient::Request* request_ = nullptr;

  private:
    TypedDynamicModuleLbConfig& parent_;
    const uint64_t callout_id_{};
    Event::Dispatcher& dispatcher_;
    const std::weak_ptr<LifetimeGate> lifetime_;
  };

  const DynamicModuleLbConfigSharedPtr config_;
  Server::Configuration::ServerFactoryContext& server_context_;
  Event::Dispatcher& dispatcher_;
  std::unique_ptr<Init::TargetImpl> init_target_;
  std::shared_ptr<LifetimeGate> lifetime_;
  bool server_initialized_requested_ = false;

  Event::TimerPtr timer_;
  std::chrono::milliseconds timer_interval_{0};
  bool timer_enabled_ = false;

  uint64_t next_callout_id_ = 1; // 0 is reserved as an invalid id.
  absl::flat_hash_map<uint64_t, std::unique_ptr<HttpCalloutCallback>> http_callouts_;
};

} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
