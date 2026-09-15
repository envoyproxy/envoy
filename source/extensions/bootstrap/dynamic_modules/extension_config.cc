#include "source/extensions/bootstrap/dynamic_modules/extension_config.h"

#include <algorithm>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/listener_manager/filter_chain_manager_impl.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

DynamicModuleBootstrapExtensionConfig::DynamicModuleBootstrapExtensionConfig(
    const absl::string_view extension_name, const absl::string_view extension_config,
    const absl::string_view metrics_namespace,
    Extensions::DynamicModules::DynamicModulePtr dynamic_module,
    Event::Dispatcher& main_thread_dispatcher, Server::Configuration::ServerFactoryContext& context,
    Stats::Store& stats_store)
    : dynamic_module_(std::move(dynamic_module)), main_thread_dispatcher_(main_thread_dispatcher),
      context_(context), stats_store_(stats_store),
      stats_scope_(stats_store.createScope(absl::StrCat(metrics_namespace, "."))),
      stat_name_pool_(stats_scope_->symbolTable()) {
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

void DynamicModuleBootstrapExtensionConfig::signalInitComplete() {
  if (init_target_ == nullptr) {
    IS_ENVOY_BUG("dynamic modules: signal_init_complete called but no init target registered");
    return;
  }
  init_target_->ready();
  ENVOY_LOG(debug, "dynamic modules: init target signaled complete, Envoy may start accepting "
                   "traffic");
}

bool DynamicModuleBootstrapExtensionConfig::enableClusterLifecycle() {
  if (cluster_lifecycle_enabled_) {
    return false;
  }
  if (!server_initialized_) {
    ENVOY_LOG(error, "cannot enable cluster lifecycle before server is initialized");
    return false;
  }
  cluster_lifecycle_enabled_ = true;
  cluster_update_callbacks_handle_ =
      context_.clusterManager().addThreadLocalClusterUpdateCallbacks(*this);
  // Register a shutdown callback to release the handle before the underlying TLS data is
  // destroyed. The TLS shutdown happens in terminate() after ShutdownExit callbacks fire.
  cluster_lifecycle_shutdown_handle_ = context_.lifecycleNotifier().registerCallback(
      Server::ServerLifecycleNotifier::Stage::ShutdownExit,
      [this]() { cluster_update_callbacks_handle_.reset(); });
  return true;
}

void DynamicModuleBootstrapExtensionConfig::onClusterAddOrUpdate(
    absl::string_view cluster_name, Upstream::ThreadLocalClusterCommand&) {
  // ThreadLocalClusterUpdateCallbacks are delivered on every worker thread (runOnAllThreads), but
  // the module hooks may only run on the main thread. Marshal there via post(), copying the name
  // since the view does not outlive the callback, and locking a weak_ptr so a post that outlives
  // the config is dropped rather than dereferencing freed memory.
  main_thread_dispatcher_.post([weak = weak_from_this(), name = std::string(cluster_name)]() {
    auto self = weak.lock();
    if (self && self->in_module_config_ != nullptr &&
        self->on_bootstrap_extension_cluster_add_or_update_ != nullptr) {
      self->on_bootstrap_extension_cluster_add_or_update_(
          self->thisAsVoidPtr(), self->in_module_config_, {name.data(), name.size()});
    }
  });
}

void DynamicModuleBootstrapExtensionConfig::onClusterRemoval(absl::string_view cluster_name) {
  // Marshal to the main thread as in onClusterAddOrUpdate. cluster_name is a view that does not
  // outlive this call, so copy it into the posted task rather than capturing the view.
  main_thread_dispatcher_.post([weak = weak_from_this(), name = std::string(cluster_name)]() {
    auto self = weak.lock();
    if (self && self->in_module_config_ != nullptr &&
        self->on_bootstrap_extension_cluster_removal_ != nullptr) {
      self->on_bootstrap_extension_cluster_removal_(self->thisAsVoidPtr(), self->in_module_config_,
                                                    {name.data(), name.size()});
    }
  });
}

bool DynamicModuleBootstrapExtensionConfig::enableListenerLifecycle() {
  if (listener_lifecycle_enabled_) {
    return false;
  }
  if (listener_manager_ == nullptr) {
    ENVOY_LOG(error, "cannot enable listener lifecycle before server is initialized");
    return false;
  }
  listener_lifecycle_enabled_ = true;
  listener_update_callbacks_handle_ = listener_manager_->addListenerUpdateCallbacks(*this);
  // Register a shutdown callback to release the handle before the ListenerManager is destroyed.
  listener_lifecycle_shutdown_handle_ = context_.lifecycleNotifier().registerCallback(
      Server::ServerLifecycleNotifier::Stage::ShutdownExit,
      [this]() { listener_update_callbacks_handle_.reset(); });
  return true;
}

void DynamicModuleBootstrapExtensionConfig::onListenerAddOrUpdate(absl::string_view listener_name,
                                                                  const Network::ListenerConfig&) {
  if (in_module_config_ != nullptr && on_bootstrap_extension_listener_add_or_update_ != nullptr) {
    on_bootstrap_extension_listener_add_or_update_(thisAsVoidPtr(), in_module_config_,
                                                   {listener_name.data(), listener_name.size()});
  }
}

void DynamicModuleBootstrapExtensionConfig::onListenerRemoval(const std::string& listener_name) {
  if (in_module_config_ != nullptr && on_bootstrap_extension_listener_removal_ != nullptr) {
    on_bootstrap_extension_listener_removal_(thisAsVoidPtr(), in_module_config_,
                                             {listener_name.data(), listener_name.size()});
  }
}

bool DynamicModuleBootstrapExtensionConfig::enableSecretLifecycle() {
  if (secret_lifecycle_enabled_) {
    return false;
  }
  if (!server_initialized_) {
    ENVOY_LOG(error, "cannot enable secret lifecycle before server is initialized");
    return false;
  }
  secret_lifecycle_enabled_ = true;
  // Subscribe to every dynamic TLS certificate secret provider. The SecretManager invokes this on
  // the main thread once for each provider that already exists and again for each one created
  // later; we then hook the provider's own update/remove callbacks (also main thread). Because
  // pre-existing providers are replayed through the same path, they get update AND removal events,
  // not just an initial add.
  context_.secretManager().setDynamicTlsCertificateSecretProviderCreatedCallback(
      [weak = weak_from_this()](const std::string& secret_name,
                                const Secret::TlsCertificateConfigProviderSharedPtr& provider) {
        if (auto self = weak.lock()) {
          self->subscribeSecretProvider(secret_name, provider);
        }
      });
  return true;
}

void DynamicModuleBootstrapExtensionConfig::subscribeSecretProvider(
    const std::string& secret_name, const Secret::TlsCertificateConfigProviderSharedPtr& provider) {
  // addUpdateCallback fires immediately if the secret is already present, and again on every
  // rotation; addRemoveCallback fires when the resource is explicitly removed. Both run on the main
  // thread. The handles are retained so the subscriptions stay live for the life of the config.
  secret_callback_handles_.push_back(
      provider->addUpdateCallback([weak = weak_from_this(), secret_name]() {
        if (auto self = weak.lock()) {
          self->onSecretAddOrUpdate(secret_name);
        }
        return absl::OkStatus();
      }));
  secret_callback_handles_.push_back(
      provider->addRemoveCallback([weak = weak_from_this(), secret_name]() {
        if (auto self = weak.lock()) {
          self->onSecretRemoval(secret_name);
        }
        return absl::OkStatus();
      }));
}

void DynamicModuleBootstrapExtensionConfig::onSecretAddOrUpdate(const std::string& secret_name) {
  if (in_module_config_ != nullptr && on_bootstrap_extension_secret_add_or_update_ != nullptr) {
    on_bootstrap_extension_secret_add_or_update_(thisAsVoidPtr(), in_module_config_,
                                                 {secret_name.data(), secret_name.size()});
  }
}

void DynamicModuleBootstrapExtensionConfig::onSecretRemoval(const std::string& secret_name) {
  if (in_module_config_ != nullptr && on_bootstrap_extension_secret_removal_ != nullptr) {
    on_bootstrap_extension_secret_removal_(thisAsVoidPtr(), in_module_config_,
                                           {secret_name.data(), secret_name.size()});
  }
}

void DynamicModuleBootstrapExtensionConfig::getActiveResourceNames(
    envoy_dynamic_module_type_bootstrap_active_resource_kind kind,
    absl::FunctionRef<void(absl::string_view)> emit) {
  // The cluster manager and listener manager are not available until the server is initialized.
  if (!server_initialized_) {
    return;
  }
  switch (kind) {
  case envoy_dynamic_module_type_bootstrap_active_resource_kind_FilterChain: {
    // Inline filter chains across all active listeners.
    if (listener_manager_ != nullptr) {
      for (Network::ListenerConfig& listener :
           listener_manager_->listeners(Server::ListenerManager::ListenerState::ACTIVE)) {
        for (absl::string_view name : listener.filterChainManager().filterChainNames()) {
          emit(name);
        }
      }
    }
    // Active FCDS filter chains. When a listener uses fcds_config its filter chains live in the
    // shared FCDS manager (not the listener's inline FilterChainManager). Emits nothing when FCDS
    // is unused (the singleton is never created).
    if (auto fcds_manager = Server::getFcdsSharedFilterChainManager(context_.singletonManager());
        fcds_manager != nullptr) {
      for (absl::string_view name : fcds_manager->activeFilterChainNames()) {
        emit(name);
      }
    }
    break;
  }
  case envoy_dynamic_module_type_bootstrap_active_resource_kind_Cluster:
    for (const auto& cluster_entry : context_.clusterManager().clusters().active_clusters_) {
      emit(cluster_entry.first);
    }
    break;
  case envoy_dynamic_module_type_bootstrap_active_resource_kind_TransportSocketMatch: {
    // A transport socket match is emitted only when present in every cluster that has matches, so a
    // match is observed only once it has landed in all the clusters that carry per-endpoint
    // matches. Clusters with no matches do not constrain the intersection.
    std::vector<std::vector<absl::string_view>> per_cluster_matches;
    for (const auto& [cluster_name, cluster] :
         context_.clusterManager().clusters().active_clusters_) {
      per_cluster_matches.push_back(cluster.get().info()->transportSocketMatcher().matchNames());
    }
    for (absl::string_view match_name : transportSocketMatchIntersection(per_cluster_matches)) {
      emit(match_name);
    }
    break;
  }
  case envoy_dynamic_module_type_bootstrap_active_resource_kind_Secret:
    for (const std::string& secret_name :
         context_.secretManager().dynamicActiveTlsCertificateSecretNames()) {
      emit(secret_name);
    }
    break;
  }
}

std::vector<absl::string_view>
DynamicModuleBootstrapExtensionConfig::transportSocketMatchIntersection(
    const std::vector<std::vector<absl::string_view>>& per_cluster_matches) {
  std::vector<absl::string_view> result;
  bool initialized = false;
  for (const auto& cluster_matches : per_cluster_matches) {
    // A cluster with no matches carries no per-endpoint matches, so it does not constrain the
    // intersection.
    if (cluster_matches.empty()) {
      continue;
    }
    if (!initialized) {
      result.assign(cluster_matches.begin(), cluster_matches.end());
      initialized = true;
      continue;
    }
    const absl::flat_hash_set<absl::string_view> names(cluster_matches.begin(),
                                                       cluster_matches.end());
    result.erase(std::remove_if(result.begin(), result.end(),
                                [&names](absl::string_view n) { return !names.contains(n); }),
                 result.end());
  }
  return result;
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
  // The cluster manager is not available during bootstrap extension creation, so accessing it
  // before the server is initialized would dereference a null cluster manager in release builds.
  if (!server_initialized_) {
    return envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound;
  }
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
    const absl::string_view metrics_namespace,
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

  auto on_drain_started = dynamic_module->getFunctionPointer<OnBootstrapExtensionDrainStartedType>(
      "envoy_dynamic_module_on_bootstrap_extension_drain_started");
  if (!on_drain_started.ok()) {
    return on_drain_started.status();
  }

  auto on_shutdown = dynamic_module->getFunctionPointer<OnBootstrapExtensionShutdownType>(
      "envoy_dynamic_module_on_bootstrap_extension_shutdown");
  if (!on_shutdown.ok()) {
    return on_shutdown.status();
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

  auto on_timer_fired = dynamic_module->getFunctionPointer<OnBootstrapExtensionTimerFiredType>(
      "envoy_dynamic_module_on_bootstrap_extension_timer_fired");
  if (!on_timer_fired.ok()) {
    return on_timer_fired.status();
  }

  auto on_file_changed = dynamic_module->getFunctionPointer<OnBootstrapExtensionFileChangedType>(
      "envoy_dynamic_module_on_bootstrap_extension_file_changed");
  if (!on_file_changed.ok()) {
    return on_file_changed.status();
  }

  auto on_admin_request = dynamic_module->getFunctionPointer<OnBootstrapExtensionAdminRequestType>(
      "envoy_dynamic_module_on_bootstrap_extension_admin_request");
  if (!on_admin_request.ok()) {
    return on_admin_request.status();
  }

  auto on_cluster_add_or_update =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionClusterAddOrUpdateType>(
          "envoy_dynamic_module_on_bootstrap_extension_cluster_add_or_update");
  if (!on_cluster_add_or_update.ok()) {
    return on_cluster_add_or_update.status();
  }

  auto on_cluster_removal =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionClusterRemovalType>(
          "envoy_dynamic_module_on_bootstrap_extension_cluster_removal");
  if (!on_cluster_removal.ok()) {
    return on_cluster_removal.status();
  }

  auto on_listener_add_or_update =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionListenerAddOrUpdateType>(
          "envoy_dynamic_module_on_bootstrap_extension_listener_add_or_update");
  if (!on_listener_add_or_update.ok()) {
    return on_listener_add_or_update.status();
  }

  auto on_listener_removal =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionListenerRemovalType>(
          "envoy_dynamic_module_on_bootstrap_extension_listener_removal");
  if (!on_listener_removal.ok()) {
    return on_listener_removal.status();
  }

  // Secret lifecycle hooks are optional per the ABI compatibility policy (abi/abi.h): an absent
  // symbol means the module does not implement the hook, not a load failure. A module that never
  // enables secret lifecycle need not export them.
  auto on_secret_add_or_update =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionSecretAddOrUpdateType>(
          "envoy_dynamic_module_on_bootstrap_extension_secret_add_or_update");
  auto on_secret_removal =
      dynamic_module->getFunctionPointer<OnBootstrapExtensionSecretRemovalType>(
          "envoy_dynamic_module_on_bootstrap_extension_secret_removal");

  auto config = std::make_shared<DynamicModuleBootstrapExtensionConfig>(
      extension_name, extension_config, metrics_namespace, std::move(dynamic_module),
      main_thread_dispatcher, context, stats_store);

  // Always register an init target so that Envoy blocks traffic until the module signals readiness.
  // This must happen before calling the module constructor so the module can call
  // signal_init_complete during config creation.
  config->init_target_ = std::make_unique<Init::TargetImpl>("dynamic_modules_bootstrap", []() {});
  context.initManager().add(*config->init_target_);

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
  config->on_bootstrap_extension_drain_started_ = on_drain_started.value();
  config->on_bootstrap_extension_shutdown_ = on_shutdown.value();
  config->on_bootstrap_extension_config_scheduled_ = on_config_scheduled.value();
  config->on_bootstrap_extension_http_callout_done_ = on_http_callout_done.value();
  config->on_bootstrap_extension_timer_fired_ = on_timer_fired.value();
  config->on_bootstrap_extension_file_changed_ = on_file_changed.value();
  config->on_bootstrap_extension_admin_request_ = on_admin_request.value();
  config->on_bootstrap_extension_cluster_add_or_update_ = on_cluster_add_or_update.value();
  config->on_bootstrap_extension_cluster_removal_ = on_cluster_removal.value();
  config->on_bootstrap_extension_listener_add_or_update_ = on_listener_add_or_update.value();
  config->on_bootstrap_extension_listener_removal_ = on_listener_removal.value();
  config->on_bootstrap_extension_secret_add_or_update_ =
      on_secret_add_or_update.ok() ? on_secret_add_or_update.value() : nullptr;
  config->on_bootstrap_extension_secret_removal_ =
      on_secret_removal.ok() ? on_secret_removal.value() : nullptr;

  config->stat_creation_frozen_ = true;

  return config;
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
