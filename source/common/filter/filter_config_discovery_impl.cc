#include "common/filter/filter_config_discovery_impl.h"

#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "common/config/utility.h"
#include "common/grpc/common.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Filter {

DynamicFilterConfigProviderImpl::DynamicFilterConfigProviderImpl(
    HttpFilterConfigSubscriptionSharedPtr&& subscription, bool require_terminal,
    absl::optional<std::string> require_type_url,
    Server::Configuration::FactoryContext& factory_context)
    : subscription_(std::move(subscription)), require_terminal_(require_terminal),
      require_type_url_(require_type_url), tls_(factory_context.threadLocal().allocateSlot()),
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
    const ProtobufWkt::Any& proto_config,
    Server::Configuration::NamedHttpFilterConfigFactory& factory) {
  if (factory.isTerminalFilter() && !require_terminal_) {
    throw EnvoyException(
        fmt::format("Error: filter config {} must not be the last in the filter chain", name()));
  } else if (!factory.isTerminalFilter() && require_terminal_) {
    throw EnvoyException(
        fmt::format("Error: filter config {} must be the last in the filter chain", name()));
  }
  if (require_type_url_.has_value()) {
    auto type_url = Config::Utility::getFactoryType(proto_config);
    if (type_url != require_type_url_.value()) {
      throw EnvoyException(fmt::format("Error: filter config has type URL {} but expect {}",
                                       type_url, require_type_url_.value()));
    }
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

HttpFilterConfigSubscription::HttpFilterConfigSubscription(
    const envoy::config::core::v3::ConfigSource& config_source,
    const std::string& filter_config_name, Server::Configuration::FactoryContext& factory_context,
    const std::string& stat_prefix,
    HttpFilterConfigProviderManagerImpl& filter_config_provider_manager,
    const std::string& subscription_id)
    : Config::SubscriptionBase<envoy::config::core::v3::TypedExtensionConfig>(
          envoy::config::core::v3::ApiVersion::V3,
          factory_context.messageValidationContext().dynamicValidationVisitor(), "name"),
      filter_config_name_(filter_config_name), factory_context_(factory_context),
      validator_(factory_context.messageValidationContext().dynamicValidationVisitor()),
      init_target_(fmt::format("HttpFilterConfigSubscription init {}", filter_config_name_),
                   [this]() { start(); }),
      scope_(factory_context.scope().createScope(stat_prefix + "filter_config_discovery." +
                                                 filter_config_name_ + ".")),
      stat_prefix_(stat_prefix), stats_({ALL_FILTER_CONFIG_DISCOVERY_STATS(POOL_COUNTER(*scope_))}),
      filter_config_provider_manager_(filter_config_provider_manager),
      subscription_id_(subscription_id) {
  const auto resource_name = getResourceName();
  subscription_ =
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          config_source, Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_);
}

void HttpFilterConfigSubscription::start() {
  if (!started_) {
    started_ = true;
    subscription_->start({filter_config_name_});
  }
}

void HttpFilterConfigSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& resources, const std::string& version_info) {
  // Make sure to make progress in case the control plane is temporarily inconsistent.
  init_target_.ready();

  if (resources.size() != 1) {
    throw EnvoyException(fmt::format(
        "Unexpected number of resources in ExtensionConfigDS response: {}", resources.size()));
  }
  const auto& filter_config = dynamic_cast<const envoy::config::core::v3::TypedExtensionConfig&>(
      resources[0].get().resource());
  if (filter_config.name() != filter_config_name_) {
    throw EnvoyException(fmt::format("Unexpected resource name in ExtensionConfigDS response: {}",
                                     filter_config.name()));
  }
  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::NamedHttpFilterConfigFactory>(
          filter_config);
  // Ensure that the filter config is valid in the filter chain context once the proto is processed.
  for (auto* provider : filter_config_providers_) {
    provider->validateConfig(filter_config.typed_config(), factory);
  }
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      filter_config.typed_config(), validator_, factory);
  Http::FilterFactoryCb factory_callback =
      factory.createFilterFactoryFromProto(*message, stat_prefix_, factory_context_);
  ENVOY_LOG(debug, "Updating filter config {}", filter_config_name_);
  stats_.config_reload_.inc();
  for (auto* provider : filter_config_providers_) {
    provider->onConfigUpdate(factory_callback, version_info);
  }
}

void HttpFilterConfigSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, const std::string&) {
  if (!removed_resources.empty()) {
    ENVOY_LOG(error,
              "Server sent a delta ExtensionConfigDS update attempting to remove a resource (name: "
              "{}). Ignoring.",
              removed_resources[0]);
  }
  if (!added_resources.empty()) {
    onConfigUpdate(added_resources, added_resources[0].get().version());
  }
}

void HttpFilterConfigSubscription::onConfigUpdateFailed(Config::ConfigUpdateFailureReason reason,
                                                        const EnvoyException*) {
  ENVOY_LOG(debug, "Updating filter config {} failed due to {}", filter_config_name_, reason);
  stats_.config_fail_.inc();
  // Make sure to make progress in case the control plane is temporarily failing.
  init_target_.ready();
}

HttpFilterConfigSubscription::~HttpFilterConfigSubscription() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  init_target_.ready();
  // Remove the subscription from the provider manager.
  filter_config_provider_manager_.subscriptions_.erase(subscription_id_);
}

std::shared_ptr<HttpFilterConfigSubscription> HttpFilterConfigProviderManagerImpl::getSubscription(
    const envoy::config::core::v3::ConfigSource& config_source, const std::string& name,
    Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix) {
  // HttpFilterConfigSubscriptions are unique based on their config source and filter config name
  // combination.
  // TODO(https://github.com/envoyproxy/envoy/issues/11967) Hash collision can cause subscription
  // aliasing.
  const std::string subscription_id = absl::StrCat(MessageUtil::hash(config_source), ".", name);
  auto it = subscriptions_.find(subscription_id);
  if (it == subscriptions_.end()) {
    auto subscription = std::make_shared<HttpFilterConfigSubscription>(
        config_source, name, factory_context, stat_prefix, *this, subscription_id);
    subscriptions_.insert(
        {subscription_id, std::weak_ptr<HttpFilterConfigSubscription>(subscription)});
    return subscription;
  } else {
    auto existing = it->second.lock();
    RELEASE_ASSERT(existing != nullptr,
                   absl::StrCat("Cannot find subscribed filter config resource ", name));
    return existing;
  }
}

HttpFilterConfigProviderPtr HttpFilterConfigProviderManagerImpl::createDynamicFilterConfigProvider(
    const envoy::config::core::v3::ConfigSource& config_source,
    const std::string& filter_config_name, bool require_terminal,
    absl::optional<std::string> require_type_url,
    Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix,
    bool apply_without_warming) {
  auto subscription =
      getSubscription(config_source, filter_config_name, factory_context, stat_prefix);
  // For warming, wait until the subscription receives the first response to indicate readiness.
  // Otherwise, mark ready immediately and start the subscription on initialization. A default
  // config is expected in the latter case.
  if (!apply_without_warming) {
    factory_context.initManager().add(subscription->initTarget());
  }
  auto provider = std::make_unique<DynamicFilterConfigProviderImpl>(
      std::move(subscription), require_terminal, require_type_url, factory_context);
  // Ensure the subscription starts if it has not already.
  if (apply_without_warming) {
    factory_context.initManager().add(provider->init_target_);
  }
  return provider;
}

} // namespace Filter
} // namespace Envoy
