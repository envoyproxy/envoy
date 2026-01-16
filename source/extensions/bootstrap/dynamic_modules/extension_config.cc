#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

DynamicModuleBootstrapExtensionConfig::DynamicModuleBootstrapExtensionConfig(
    const absl::string_view extension_name, const absl::string_view extension_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Event::Dispatcher& main_thread_dispatcher, Server::Configuration::ServerFactoryContext& context,
    Stats::Store& stats_store)
    : dynamic_module_(std::move(dynamic_module)), main_thread_dispatcher_(main_thread_dispatcher),
      context_(context), stats_store_(stats_store) {
  ASSERT(dynamic_module_ != nullptr);
  ASSERT(extension_name.data() != nullptr);
  ASSERT(extension_config.data() != nullptr);
}

DynamicModuleBootstrapExtensionConfig::~DynamicModuleBootstrapExtensionConfig() {
  // Cancel any pending HTTP callouts before destroying the config.
  for (auto& callout : http_callouts_) {
    if (callout.second->request_ != nullptr) {
      callout.second->request_->cancel();
    }
  }
  http_callouts_.clear();

  if (in_module_config_ != nullptr && on_bootstrap_extension_config_destroy_ != nullptr) {
    on_bootstrap_extension_config_destroy_(in_module_config_);
  }
}

void DynamicModuleBootstrapExtensionConfig::onScheduled(uint64_t event_id) {
  if (in_module_config_ != nullptr && on_bootstrap_extension_config_scheduled_ != nullptr) {
    on_bootstrap_extension_config_scheduled_(thisAsVoidPtr(), in_module_config_, event_id);
  }
}

envoy_dynamic_module_type_http_callout_init_result
DynamicModuleBootstrapExtensionConfig::sendHttpCallout(uint64_t* callout_id_out,
                                                       absl::string_view cluster_name,
                                                       Http::RequestMessagePtr&& message,
                                                       uint64_t timeout_milliseconds) {
  // Access cluster manager lazily since it's not available during bootstrap extension creation.
  Upstream::ThreadLocalCluster* cluster =
      context_.clusterManager().getThreadLocalCluster(cluster_name);
  if (!cluster) {
    return envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound;
  }
  Http::AsyncClient::RequestOptions options;
  options.setTimeout(std::chrono::milliseconds(timeout_milliseconds));

  // Prepare the callback and the ID.
  const uint64_t callout_id = getNextCalloutId();
  auto http_callout_callback =
      std::make_unique<DynamicModuleBootstrapExtensionConfig::HttpCalloutCallback>(
          shared_from_this(), callout_id);
  DynamicModuleBootstrapExtensionConfig::HttpCalloutCallback& callback = *http_callout_callback;

  auto request = cluster->httpAsyncClient().send(std::move(message), callback, options);
  if (!request) {
    return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
  }

  // Register the callout.
  callback.request_ = request;
  http_callouts_.emplace(callout_id, std::move(http_callout_callback));
  *callout_id_out = callout_id;

  return envoy_dynamic_module_type_http_callout_init_result_Success;
}

void DynamicModuleBootstrapExtensionConfig::HttpCalloutCallback::onSuccess(
    const Http::AsyncClient::Request&, Http::ResponseMessagePtr&& response) {
  // Move the config and callout id to the local scope since
  // on_bootstrap_extension_http_callout_done_ might result in operations that affect this
  // callback's lifetime.
  DynamicModuleBootstrapExtensionConfigSharedPtr config = std::move(config_);
  uint64_t callout_id = callout_id_;

  // Check if the config still has the in-module config.
  if (!config->in_module_config_) {
    config->http_callouts_.erase(callout_id);
    return;
  }

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
  config->on_bootstrap_extension_http_callout_done_(
      config->thisAsVoidPtr(), config->in_module_config_, callout_id,
      envoy_dynamic_module_type_http_callout_result_Success, headers_vector.data(),
      headers_vector.size(), reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(body.data()),
      body.size());
  // Clean up the callout.
  config->http_callouts_.erase(callout_id);
}

void DynamicModuleBootstrapExtensionConfig::HttpCalloutCallback::onFailure(
    const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason reason) {
  // Move the config and callout id to the local scope since
  // on_bootstrap_extension_http_callout_done_ might result in operations that affect this
  // callback's lifetime.
  DynamicModuleBootstrapExtensionConfigSharedPtr config = std::move(config_);
  const uint64_t callout_id = callout_id_;

  // Check if the config still has the in-module config.
  if (!config->in_module_config_) {
    config->http_callouts_.erase(callout_id);
    return;
  }

  // request_ is not null if the callout is actually sent to the upstream cluster.
  // This allows us to avoid inlined calls to onFailure() method (which results in a reentrant to
  // the modules) when the async client immediately fails the callout.
  if (request_) {
    envoy_dynamic_module_type_http_callout_result result;
    switch (reason) {
    case Http::AsyncClient::FailureReason::Reset:
      result = envoy_dynamic_module_type_http_callout_result_Reset;
      break;
    case Http::AsyncClient::FailureReason::ExceedResponseBufferLimit:
      result = envoy_dynamic_module_type_http_callout_result_ExceedResponseBufferLimit;
      break;
    }
    config->on_bootstrap_extension_http_callout_done_(config->thisAsVoidPtr(),
                                                      config->in_module_config_, callout_id, result,
                                                      nullptr, 0, nullptr, 0);
  }

  // Clean up the callout.
  config->http_callouts_.erase(callout_id);
}

absl::StatusOr<DynamicModuleBootstrapExtensionConfigSharedPtr>
newDynamicModuleBootstrapExtensionConfig(
    const absl::string_view extension_name, const absl::string_view extension_config,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Event::Dispatcher& main_thread_dispatcher, Server::Configuration::ServerFactoryContext& context,
    Stats::Store& stats_store) {

  // Resolve the required symbols from the dynamic module.
  auto constructor =
      dynamic_module
          ->getFunctionPointer<decltype(&envoy_dynamic_module_on_bootstrap_extension_config_new)>(
              "envoy_dynamic_module_on_bootstrap_extension_config_new");
  if (!constructor.ok()) {
    return constructor.status();
  }

  auto on_config_destroy =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionConfigDestroyType>(
          "envoy_dynamic_module_on_bootstrap_extension_config_destroy");
  if (!on_config_destroy.ok()) {
    return on_config_destroy.status();
  }

  auto on_extension_new = dynamic_module->getFunctionPointer<OnBootstrapExtensionNewType>(
      "envoy_dynamic_module_on_bootstrap_extension_new");
  if (!on_extension_new.ok()) {
    return on_extension_new.status();
  }

  auto on_server_initialized =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionServerInitializedType>(
          "envoy_dynamic_module_on_bootstrap_extension_server_initialized");
  if (!on_server_initialized.ok()) {
    return on_server_initialized.status();
  }

  auto on_worker_thread_initialized =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionWorkerThreadInitializedType>(
          "envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized");
  if (!on_worker_thread_initialized.ok()) {
    return on_worker_thread_initialized.status();
  }

  auto on_extension_destroy = dynamic_module->getFunctionPointer<OnBootstrapExtensionDestroyType>(
      "envoy_dynamic_module_on_bootstrap_extension_destroy");
  if (!on_extension_destroy.ok()) {
    return on_extension_destroy.status();
  }

  auto on_config_scheduled =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionConfigScheduledType>(
          "envoy_dynamic_module_on_bootstrap_extension_config_scheduled");
  if (!on_config_scheduled.ok()) {
    return on_config_scheduled.status();
  }

  auto on_http_callout_done =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionHttpCalloutDoneType>(
          "envoy_dynamic_module_on_bootstrap_extension_http_callout_done");
  if (!on_http_callout_done.ok()) {
    return on_http_callout_done.status();
  }

  auto config = std::make_shared<DynamicModuleBootstrapExtensionConfig>(
      extension_name, extension_config, std::move(dynamic_module), main_thread_dispatcher, context,
      stats_store);

  const void* extension_config_module_ptr = (*constructor.value())(
      static_cast<void*>(config.get()), {extension_name.data(), extension_name.size()},
      {extension_config.data(), extension_config.size()});
  if (extension_config_module_ptr == nullptr) {
    return absl::InvalidArgumentError("Failed to initialize dynamic module");
  }

  config->in_module_config_ = extension_config_module_ptr;
  config->on_bootstrap_extension_config_destroy_ = on_config_destroy.value();
  config->on_bootstrap_extension_new_ = on_extension_new.value();
  config->on_bootstrap_extension_server_initialized_ = on_server_initialized.value();
  config->on_bootstrap_extension_worker_thread_initialized_ = on_worker_thread_initialized.value();
  config->on_bootstrap_extension_destroy_ = on_extension_destroy.value();
  config->on_bootstrap_extension_config_scheduled_ = on_config_scheduled.value();
  config->on_bootstrap_extension_http_callout_done_ = on_http_callout_done.value();

  return config;
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
