#include "source/extensions/load_balancing_policies/dynamic_modules/lb_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/thread.h"

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DynamicModules {

// =================================================================================================
// DynamicModuleLbConfig
// =================================================================================================

absl::StatusOr<DynamicModuleLbConfigSharedPtr>
DynamicModuleLbConfig::create(const std::string& lb_policy_name, const std::string& lb_config,
                              const std::string& metrics_namespace,
                              Envoy::Extensions::DynamicModules::DynamicModulePtr module,
                              Stats::Scope& stats_scope) {
  std::shared_ptr<DynamicModuleLbConfig> config(new DynamicModuleLbConfig(
      lb_policy_name, lb_config, metrics_namespace, std::move(module), stats_scope));

  // Resolve all required function pointers from the dynamic module.
#define RESOLVE_SYMBOL(name, type, member)                                                         \
  {                                                                                                \
    auto symbol_or_error = config->dynamic_module_->getFunctionPointer<type>(name);                \
    if (!symbol_or_error.ok()) {                                                                   \
      return symbol_or_error.status();                                                             \
    }                                                                                              \
    config->member = symbol_or_error.value();                                                      \
  }

  RESOLVE_SYMBOL("envoy_dynamic_module_on_lb_config_new", OnLbConfigNewType, on_config_new_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_lb_config_destroy", OnLbConfigDestroyType,
                 on_config_destroy_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_lb_new", OnLbNewType, on_lb_new_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_lb_choose_host", OnLbChooseHostType, on_choose_host_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_lb_on_host_membership_update",
                 OnLbOnHostMembershipUpdateType, on_host_membership_update_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_lb_destroy", OnLbDestroyType, on_lb_destroy_);

#undef RESOLVE_SYMBOL

  // Optional. A missing symbol leaves the pointer null.
#define RESOLVE_OPTIONAL_SYMBOL(name, type, member)                                                \
  if (auto symbol_or_error = config->dynamic_module_->getFunctionPointer<type>(name);              \
      symbol_or_error.ok()) {                                                                      \
    config->member = symbol_or_error.value();                                                      \
  }

  RESOLVE_OPTIONAL_SYMBOL("envoy_dynamic_module_on_lb_config_http_callout_done",
                          OnLbConfigHttpCalloutDoneType, on_http_callout_done_);
  RESOLVE_OPTIONAL_SYMBOL("envoy_dynamic_module_on_lb_config_timer", OnLbConfigTimerType,
                          on_timer_);
  RESOLVE_OPTIONAL_SYMBOL("envoy_dynamic_module_on_lb_config_server_initialized",
                          OnLbConfigServerInitializedType, on_server_initialized_);

#undef RESOLVE_OPTIONAL_SYMBOL

  return config;
}

absl::Status DynamicModuleLbConfig::initializeInModuleConfig() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  envoy_dynamic_module_type_envoy_buffer name_buffer = {lb_policy_name_.data(),
                                                        lb_policy_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buffer = {lb_config_.data(), lb_config_.size()};

  in_module_config_ = on_config_new_(this, name_buffer, config_buffer);
  if (in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("failed to create in-module load balancer configuration");
  }

  stat_creation_frozen_ = true;
  return absl::OkStatus();
}

DynamicModuleLbConfig::DynamicModuleLbConfig(
    const std::string& lb_policy_name, const std::string& lb_config,
    const std::string& metrics_namespace,
    Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module, Stats::Scope& stats_scope)
    : stats_scope_(stats_scope.createScope(absl::StrCat(metrics_namespace, "."))),
      metrics_(*stats_scope_), lb_policy_name_(lb_policy_name), lb_config_(lb_config),
      dynamic_module_(std::move(dynamic_module)) {}

DynamicModuleLbConfig::~DynamicModuleLbConfig() {
  if (in_module_config_ != nullptr && on_config_destroy_ != nullptr) {
    on_config_destroy_(in_module_config_);
  }
}

envoy_dynamic_module_type_http_callout_init_result
DynamicModuleLbConfig::sendHttpCallout(uint64_t* callout_id_out, absl::string_view cluster_name,
                                       Http::RequestMessagePtr&& message,
                                       uint64_t timeout_milliseconds) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (main_thread_state_ == nullptr) {
    return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
  }
  return main_thread_state_->sendHttpCallout(callout_id_out, cluster_name, std::move(message),
                                             timeout_milliseconds);
}

void DynamicModuleLbConfig::enableTimer(uint64_t interval_milliseconds) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (main_thread_state_ == nullptr) {
    return;
  }
  main_thread_state_->enableTimer(interval_milliseconds);
}

void DynamicModuleLbConfig::disableTimer() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (main_thread_state_ == nullptr) {
    return;
  }
  main_thread_state_->disableTimer();
}

void DynamicModuleLbConfig::scheduleOnServerInitialized() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (main_thread_state_ == nullptr) {
    return;
  }
  main_thread_state_->scheduleOnServerInitialized();
}

// =================================================================================================
// TypedDynamicModuleLbConfig
// =================================================================================================

absl::StatusOr<std::unique_ptr<TypedDynamicModuleLbConfig>>
TypedDynamicModuleLbConfig::create(DynamicModuleLbConfigSharedPtr config,
                                   Server::Configuration::ServerFactoryContext& server_context) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  auto typed_config = std::unique_ptr<TypedDynamicModuleLbConfig>(
      new TypedDynamicModuleLbConfig(std::move(config), server_context));
  RETURN_IF_NOT_OK(typed_config->config_->initializeInModuleConfig());
  typed_config->armServerInitializedHook();
  return typed_config;
}

TypedDynamicModuleLbConfig::TypedDynamicModuleLbConfig(
    DynamicModuleLbConfigSharedPtr config,
    Server::Configuration::ServerFactoryContext& server_context)
    : config_(std::move(config)), server_context_(server_context),
      dispatcher_(server_context.mainThreadDispatcher()),
      lifetime_(std::make_shared<LifetimeGate>()) {
  config_->setMainThreadState(this);
}

void TypedDynamicModuleLbConfig::scheduleOnServerInitialized() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  server_initialized_requested_ = true;
}

void TypedDynamicModuleLbConfig::armServerInitializedHook() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (!server_initialized_requested_ || config_->on_server_initialized_ == nullptr) {
    return;
  }

  const auto invoke = [this]() {
    config_->on_server_initialized_(config_.get(), config_->in_module_config_);
  };
  // Register an init target during server startup, or invoke the hook directly if already
  // initialized.
  if (server_context_.initManager().state() == Init::Manager::State::Uninitialized) {
    init_target_ =
        std::make_unique<Init::TargetImpl>("dynamic_modules_lb_config", [this, invoke]() {
          invoke();
          init_target_->ready();
        });
    server_context_.initManager().add(*init_target_);
  } else {
    invoke();
  }
}

TypedDynamicModuleLbConfig::~TypedDynamicModuleLbConfig() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  lifetime_.reset();
  // Keep the callback alive across cancel() because it still accesses it during cleanup.
  while (!http_callouts_.empty()) {
    auto it = http_callouts_.begin();
    auto callout = std::move(it->second);
    http_callouts_.erase(it);
    if (callout->request_ != nullptr) {
      auto request = callout->request_;
      callout->request_ = nullptr;
      request->cancel();
    }
  }
  timer_.reset();
  config_->setMainThreadState(nullptr);
}

envoy_dynamic_module_type_http_callout_init_result TypedDynamicModuleLbConfig::sendHttpCallout(
    uint64_t* callout_id_out, absl::string_view cluster_name, Http::RequestMessagePtr&& message,
    uint64_t timeout_milliseconds) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (server_context_.initManager().state() == Init::Manager::State::Uninitialized) {
    return envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound;
  }

  if (config_->on_http_callout_done_ == nullptr) {
    ENVOY_LOG(debug, "dynamic module lb config: HTTP callout requested but "
                     "on_lb_config_http_callout_done is not implemented.");
    return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
  }

  Upstream::ThreadLocalCluster* cluster =
      server_context_.clusterManager().getThreadLocalCluster(cluster_name);
  if (!cluster) {
    return envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound;
  }
  Http::AsyncClient::RequestOptions options;
  options.setTimeout(std::chrono::milliseconds(timeout_milliseconds));

  const uint64_t callout_id = getNextCalloutId();
  auto http_callout_callback =
      std::make_unique<HttpCalloutCallback>(*this, callout_id, dispatcher_, lifetime_);
  HttpCalloutCallback& callback = *http_callout_callback;

  auto request = cluster->httpAsyncClient().send(std::move(message), callback, options);
  if (!request) {
    return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
  }

  callback.request_ = request;
  http_callouts_.emplace(callout_id, std::move(http_callout_callback));
  *callout_id_out = callout_id;

  return envoy_dynamic_module_type_http_callout_init_result_Success;
}

void TypedDynamicModuleLbConfig::HttpCalloutCallback::onSuccess(
    const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) {
  // Extract the callback from the map before notifying the module.
  const uint64_t callout_id = callout_id_;
  TypedDynamicModuleLbConfig& parent = parent_;
  auto it = parent.http_callouts_.find(callout_id);
  if (it == parent.http_callouts_.end()) {
    return;
  }
  auto callback = std::move(it->second);
  parent.http_callouts_.erase(it);

  const DynamicModuleLbConfigSharedPtr& config = parent.config_;
  ASSERT(config->in_module_config_ != nullptr);
  ASSERT(config->on_http_callout_done_ != nullptr);

  absl::InlinedVector<envoy_dynamic_module_type_envoy_http_header, 16> headers_vector;
  headers_vector.reserve(response->headers().size());
  response->headers().iterate([&headers_vector](
                                  const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    headers_vector.emplace_back(envoy_dynamic_module_type_envoy_http_header{
        const_cast<char*>(header.key().getStringView().data()), header.key().getStringView().size(),
        const_cast<char*>(header.value().getStringView().data()),
        header.value().getStringView().size()});
    return Http::HeaderMap::Iterate::Continue;
  });

  Envoy::Buffer::RawSliceVector body = response->body().getRawSlices(std::nullopt);
  config->on_http_callout_done_(
      config.get(), config->in_module_config_, callout_id,
      envoy_dynamic_module_type_http_callout_result_Success, headers_vector.data(),
      headers_vector.size(), reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(body.data()),
      body.size());
}

void TypedDynamicModuleLbConfig::HttpCalloutCallback::onFailure(
    const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason reason) {
  // Extract the callback from the map before notifying the module.
  const uint64_t callout_id = callout_id_;
  TypedDynamicModuleLbConfig& parent = parent_;
  auto it = parent.http_callouts_.find(callout_id);
  if (it == parent.http_callouts_.end()) {
    return;
  }
  auto callback = std::move(it->second);
  parent.http_callouts_.erase(it);

  const DynamicModuleLbConfigSharedPtr config = parent.config_;
  ASSERT(config->in_module_config_ != nullptr);
  ASSERT(config->on_http_callout_done_ != nullptr);

  envoy_dynamic_module_type_http_callout_result result;
  switch (reason) {
  case Http::AsyncClient::FailureReason::Reset:
    result = envoy_dynamic_module_type_http_callout_result_Reset;
    break;
  case Http::AsyncClient::FailureReason::ExceedResponseBufferLimit:
    result = envoy_dynamic_module_type_http_callout_result_ExceedResponseBufferLimit;
    break;
  }
  // Post to the dispatcher to avoid reentrancy.
  auto lifetime = lifetime_;
  dispatcher_.post([lifetime, config, callout_id, result]() {
    if (!lifetime.lock()) {
      return;
    }
    ASSERT(config->in_module_config_ != nullptr);
    ASSERT(config->on_http_callout_done_ != nullptr);
    config->on_http_callout_done_(config.get(), config->in_module_config_, callout_id, result,
                                  nullptr, 0, nullptr, 0);
  });
}

void TypedDynamicModuleLbConfig::enableTimer(uint64_t interval_milliseconds) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (config_->on_timer_ == nullptr) {
    ENVOY_LOG(debug, "dynamic module lb config: timer requested but on_lb_config_timer is not "
                     "implemented.");
    return;
  }
  timer_interval_ = std::chrono::milliseconds(interval_milliseconds);
  timer_enabled_ = true;
  if (timer_ == nullptr) {
    timer_ = dispatcher_.createTimer([this]() { onTimer(); });
  }
  timer_->enableTimer(timer_interval_);
}

void TypedDynamicModuleLbConfig::disableTimer() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  timer_enabled_ = false;
  if (timer_ != nullptr) {
    timer_->disableTimer();
  }
}

void TypedDynamicModuleLbConfig::onTimer() {
  ASSERT(config_->in_module_config_ != nullptr);
  ASSERT(config_->on_timer_ != nullptr);
  config_->on_timer_(config_.get(), config_->in_module_config_);
  if (timer_enabled_ && timer_) {
    timer_->enableTimer(timer_interval_);
  }
}

} // namespace DynamicModules
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
