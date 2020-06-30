#include "common/router/filter_config_discovery_impl.h"

#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "common/config/utility.h"
#include "common/grpc/common.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Router {

DynamicFilterConfigProviderImpl::DynamicFilterConfigProviderImpl(
    FilterConfigSubscriptionSharedPtr&& subscription, bool require_terminal,
    Server::Configuration::FactoryContext& factory_context)
    : subscription_(std::move(subscription)), require_terminal_(require_terminal),
      tls_(factory_context.threadLocal().allocateSlot()),
      init_target_("DynamicFilterConfigProviderImpl", [this]() {
        subscription_->start();
        init_target_.ready();
      }) {
  subscription_->filter_config_providers_.insert(this);
  tls_->set([](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalConfig>();
  });
}

DynamicFilterConfigProviderImpl::~DynamicFilterConfigProviderImpl() {
  subscription_->filter_config_providers_.erase(this);
}

const std::string& DynamicFilterConfigProviderImpl::name() { return subscription_->name(); }

absl::optional<Http::FilterFactoryCb> DynamicFilterConfigProviderImpl::config() {
  return tls_->getTyped<ThreadLocalConfig>().config_;
}

void DynamicFilterConfigProviderImpl::validateConfig(
    Server::Configuration::NamedHttpFilterConfigFactory& factory) {
  if (factory.isTerminalFilter() && !require_terminal_) {
    throw EnvoyException(
        fmt::format("Error: filter config {} must not be the last in the filter chain", name()));
  } else if (!factory.isTerminalFilter() && require_terminal_) {
    throw EnvoyException(
        fmt::format("Error: filter config {} must be the last in the filter chain", name()));
  }
}

void DynamicFilterConfigProviderImpl::onConfigUpdate(Http::FilterFactoryCb config,
                                                     const std::string&) {
  tls_->runOnAllThreads([config](ThreadLocal::ThreadLocalObjectSharedPtr previous)
                            -> ThreadLocal::ThreadLocalObjectSharedPtr {
    auto prev_config = std::dynamic_pointer_cast<ThreadLocalConfig>(previous);
    prev_config->config_ = config;
    return previous;
  });
}

FilterConfigSubscription::FilterConfigSubscription(
    const envoy::config::core::v3::ConfigSource& config_source,
    const std::string& filter_config_name, Server::Configuration::FactoryContext& factory_context,
    const std::string& stat_prefix, FilterConfigProviderManagerImpl& filter_config_provider_manager,
    const std::string& subscription_id)
    : Envoy::Config::SubscriptionBase<envoy::config::core::v3::TypedExtensionConfig>(
          envoy::config::core::v3::ApiVersion::V3),
      filter_config_name_(filter_config_name), factory_context_(factory_context),
      validator_(factory_context.messageValidationContext().dynamicValidationVisitor()),
      parent_init_target_(fmt::format("FilterConfigSubscription init {}", filter_config_name_),
                          [this]() { local_init_manager_.initialize(local_init_watcher_); }),
      local_init_watcher_(
          fmt::format("FilterConfigSubscription local-init-watcher {}", filter_config_name_),
          [this]() { parent_init_target_.ready(); }),
      local_init_target_(
          fmt::format("FilterConfigSubscription local-init-target {}", filter_config_name_),
          [this]() { start(); }),
      local_init_manager_(
          fmt::format("FilterConfigSubscription local-init-manager {}", filter_config_name_)),
      scope_(factory_context.scope().createScope(stat_prefix + "filter_config_discovery." +
                                                 filter_config_name_ + ".")),
      stat_prefix_(stat_prefix), filter_config_provider_manager_(filter_config_provider_manager),
      subscription_id_(subscription_id) {
  const auto resource_name = getResourceName();
  subscription_ =
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          config_source, Grpc::Common::typeUrl(resource_name), *scope_, *this);
  local_init_manager_.add(local_init_target_);
}

void FilterConfigSubscription::start() {
  if (!started_) {
    started_ = true;
    subscription_->start({filter_config_name_});
  }
}

void FilterConfigSubscription::onConfigUpdate(
    const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
    const std::string& version_info) {
  // Make sure to make progress in case the control plane is temporarily inconsistent.
  local_init_target_.ready();

  if (resources.size() != 1) {
    throw EnvoyException(fmt::format(
        "Unexpected number of resources in FilterConfigDS response: {}", resources.size()));
  }
  auto filter_config =
      MessageUtil::anyConvertAndValidate<envoy::config::core::v3::TypedExtensionConfig>(
          resources[0], validator_);
  if (filter_config.name() != filter_config_name_) {
    throw EnvoyException(fmt::format("Unexpected resource name in FilterConfigDS response: {}",
                                     filter_config.name()));
  }
  auto& factory = Envoy::Config::Utility::getAndCheckFactory<
      Server::Configuration::NamedHttpFilterConfigFactory>(filter_config);
  // Ensure that the factory is valid in the filter chain context.
  for (auto* provider : filter_config_providers_) {
    provider->validateConfig(factory);
  }
  ProtobufTypes::MessagePtr message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      filter_config.typed_config(), validator_, factory);
  Http::FilterFactoryCb factory_callback =
      factory.createFilterFactoryFromProto(*message, stat_prefix_, factory_context_);
  ENVOY_LOG(debug, "Updating filter config {}", filter_config_name_);
  for (auto* provider : filter_config_providers_) {
    provider->onConfigUpdate(factory_callback, version_info);
  }
}

void FilterConfigSubscription::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, const std::string&) {
  if (!removed_resources.empty()) {
    ENVOY_LOG(error,
              "Server sent a delta FilterConfigDS update attempting to remove a resource (name: "
              "{}). Ignoring.",
              removed_resources[0]);
  }
  if (!added_resources.empty()) {
    Protobuf::RepeatedPtrField<ProtobufWkt::Any> unwrapped_resource;
    *unwrapped_resource.Add() = added_resources[0].resource();
    onConfigUpdate(unwrapped_resource, added_resources[0].version());
  }
}

void FilterConfigSubscription::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                                    const EnvoyException*) {
  ENVOY_LOG(debug, "Updating filter config {} failed due to {}", filter_config_name_, reason);
  // Make sure to make progress in case the control plane is temporarily failing.
  local_init_target_.ready();
}

FilterConfigSubscription::~FilterConfigSubscription() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  local_init_target_.ready();
  // Remove the subscription from the provider manager.
  filter_config_provider_manager_.subscriptions_.erase(subscription_id_);
}

std::shared_ptr<FilterConfigSubscription> FilterConfigProviderManagerImpl::getSubscription(
    const envoy::config::core::v3::ConfigSource& config_source, const std::string& name,
    Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix) {
  // FilterConfigSubscriptions are unique based on their config source and filter config name
  // combination.
  const std::string subscription_id = absl::StrCat(MessageUtil::hash(config_source), ".", name);
  auto it = subscriptions_.find(subscription_id);
  if (it == subscriptions_.end()) {
    auto subscription = std::make_shared<FilterConfigSubscription>(
        config_source, name, factory_context, stat_prefix, *this, subscription_id);
    subscriptions_.insert({subscription_id, std::weak_ptr<FilterConfigSubscription>(subscription)});
    return subscription;
  } else {
    auto existing = it->second.lock();
    RELEASE_ASSERT(existing != nullptr,
                   absl::StrCat("Cannot find subscribed filter config resource ", name));
    return existing;
  }
}

FilterConfigProviderPtr FilterConfigProviderManagerImpl::createDynamicFilterConfigProvider(
    const envoy::config::core::v3::ConfigSource& config_source,
    const std::string& filter_config_name, bool require_terminal,
    Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix,
    bool apply_without_warming) {
  auto subscription =
      getSubscription(config_source, filter_config_name, factory_context, stat_prefix);
  if (!apply_without_warming) {
    factory_context.initManager().add(subscription->initTarget());
  }
  auto provider = std::make_unique<DynamicFilterConfigProviderImpl>(
      std::move(subscription), require_terminal, factory_context);
  // Ensure the subscription starts if it has not already.
  if (apply_without_warming) {
    factory_context.initManager().add(provider->init_target_);
  }
  return provider;
}

} // namespace Router
} // namespace Envoy
