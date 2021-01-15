#include "common/filter/config_discovery_impl.h"

#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "common/grpc/common.h"
#include "common/protobuf/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Filter {

DynamicFilterConfigProviderImplBase::DynamicFilterConfigProviderImplBase(
    FilterConfigSubscriptionSharedPtr&& subscription,
    const std::set<std::string>& require_type_urls)
    : subscription_(std::move(subscription)), require_type_urls_(require_type_urls),
      init_target_("DynamicFilterConfigProviderImpl", [this]() {
        subscription_->start();
        // This init target is used to activate the subscription but not wait
        // for a response. It is used whenever a default config is provided to be
        // used while waiting for a response.
        init_target_.ready();
      }) {
  subscription_->filter_config_providers_.insert(this);
}

DynamicFilterConfigProviderImplBase::~DynamicFilterConfigProviderImplBase() {
  subscription_->filter_config_providers_.erase(this);
}

const std::string& DynamicFilterConfigProviderImplBase::name() { return subscription_->name(); }

void DynamicFilterConfigProviderImplBase::validateConfig(const ProtobufWkt::Any& proto_config) {
  const auto type_url = Config::Utility::getFactoryType(proto_config);
  if (require_type_urls_.count(type_url) == 0) {
    throw EnvoyException(fmt::format("Error: filter config has type URL {} but expect {}.",
                                     type_url, absl::StrJoin(require_type_urls_, ", ")));
  }
}

std::shared_ptr<FilterConfigSubscription> FilterConfigProviderManagerImplBase::getSubscription(
    const envoy::config::core::v3::ConfigSource& config_source, const std::string& name,
    Server::Configuration::FactoryContext& factory_context, const std::string& stat_prefix) {
  // FilterConfigSubscriptions are unique based on their config source and filter config name
  // combination.
  // TODO(https://github.com/envoyproxy/envoy/issues/11967) Hash collision can cause subscription
  // aliasing.
  const std::string subscription_id = absl::StrCat(MessageUtil::hash(config_source), ".", name);
  auto it = subscriptions_.find(subscription_id);
  if (it == subscriptions_.end()) {
    auto subscription = std::make_shared<FilterConfigSubscription>(
        config_source, name, factory_context, stat_prefix, *this, subscription_id);
    subscriptions_.insert({subscription_id, std::weak_ptr<FilterConfigSubscription>(subscription)});
    return subscription;
  } else {
    auto existing = it->second.lock();
    ASSERT(existing != nullptr,
           absl::StrCat("Cannot find subscribed filter config resource ", name));
    return existing;
  }
}

std::set<std::string> FilterConfigProviderManagerImplBase::validateConfigSource(
    const std::string& filter_config_name,
    const envoy::config::core::v3::ExtensionConfigSource& config_source, bool is_terminal) const {
  if (config_source.apply_default_config_without_warming() && !config_source.has_default_config()) {
    throw EnvoyException(
        fmt::format("Error: filter config {} applied without warming but has no default config.",
                    filter_config_name));
  }
  std::set<std::string> require_type_urls;
  for (const auto& type_url : config_source.type_urls()) {
    auto factory_type_url = TypeUtil::typeUrlToDescriptorFullName(type_url);
    require_type_urls.emplace(factory_type_url);
    validateTypeUrl(filter_config_name, factory_type_url, is_terminal);
  }
  return require_type_urls;
}

FilterConfigSubscription::FilterConfigSubscription(
    const envoy::config::core::v3::ConfigSource& config_source,
    const std::string& filter_config_name, Server::Configuration::FactoryContext& factory_context,
    const std::string& stat_prefix,
    FilterConfigProviderManagerImplBase& filter_config_provider_manager,
    const std::string& subscription_id)
    : Config::SubscriptionBase<envoy::config::core::v3::TypedExtensionConfig>(
          envoy::config::core::v3::ApiVersion::V3,
          factory_context.messageValidationContext().dynamicValidationVisitor(), "name"),
      filter_config_name_(filter_config_name), factory_context_(factory_context),
      init_target_(fmt::format("FilterConfigSubscription init {}", filter_config_name_),
                   [this]() { start(); }),
      scope_(factory_context.scope().createScope(stat_prefix + "extension_config_discovery." +
                                                 filter_config_name_ + ".")),
      stat_prefix_(stat_prefix),
      stats_({ALL_EXTENSION_CONFIG_DISCOVERY_STATS(POOL_COUNTER(*scope_))}),
      filter_config_provider_manager_(filter_config_provider_manager),
      subscription_id_(subscription_id) {
  const auto resource_name = getResourceName();
  subscription_ =
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          config_source, Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_,
          false);
}

void FilterConfigSubscription::start() {
  if (!started_) {
    started_ = true;
    subscription_->start({filter_config_name_});
  }
}

void FilterConfigSubscription::onConfigUpdate(
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
  // Skip update if hash matches
  const uint64_t new_hash = MessageUtil::hash(filter_config.typed_config());
  if (new_hash == last_config_hash_) {
    return;
  }
  // Ensure that the filter config is valid in the filter chain context once the proto is processed.
  // Validation happens before updating to prevent a partial update application. It might be
  // possible that the providers have distinct type URL constraints.
  for (auto* provider : filter_config_providers_) {
    provider->validateConfig(filter_config.typed_config());
  }
  ENVOY_LOG(debug, "Updating filter config {}", filter_config_name_);
  const auto pending_update = std::make_shared<std::atomic<uint64_t>>(
      (factory_context_.admin().concurrency() + 1) * filter_config_providers_.size());
  for (auto* provider : filter_config_providers_) {
    provider->onConfigUpdate(filter_config.typed_config(), version_info, [this, pending_update]() {
      if (--(*pending_update) == 0) {
        stats_.config_reload_.inc();
      }
    });
  }
  last_config_hash_ = new_hash;
}

void FilterConfigSubscription::onConfigUpdate(
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

void FilterConfigSubscription::onConfigUpdateFailed(Config::ConfigUpdateFailureReason reason,
                                                    const EnvoyException*) {
  ENVOY_LOG(debug, "Updating filter config {} failed due to {}", filter_config_name_, reason);
  stats_.config_fail_.inc();
  // Make sure to make progress in case the control plane is temporarily failing.
  init_target_.ready();
}

FilterConfigSubscription::~FilterConfigSubscription() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  init_target_.ready();
  // Remove the subscription from the provider manager.
  filter_config_provider_manager_.subscriptions_.erase(subscription_id_);
}

Http::FilterFactoryCb HttpFilterConfigProviderManagerImpl::instantiateFilterFactory(
    const ProtobufWkt::Any& proto_config, const std::string& stat_prefix,
    Server::Configuration::FactoryContext& factory_context) const {
  auto& factory = Config::Utility::getAndCheckFactoryByType<
      Server::Configuration::NamedHttpFilterConfigFactory>(proto_config);
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      proto_config, factory_context.messageValidationContext().dynamicValidationVisitor(), factory);
  return factory.createFilterFactoryFromProto(*message, stat_prefix, factory_context);
}

void HttpFilterConfigProviderManagerImpl::validateTypeUrl(const std::string& filter_config_name,
                                                          absl::string_view type_url,
                                                          bool is_terminal) const {
  auto* factory = Registry::FactoryRegistry<
      Server::Configuration::NamedHttpFilterConfigFactory>::getFactoryByType(type_url);
  if (factory == nullptr) {
    throw EnvoyException(
        fmt::format("Error: no factory found for a required type URL {}.", type_url));
  }
  Config::Utility::validateTerminalFilters(filter_config_name, factory->name(), "http",
                                           factory->isTerminalFilter(), is_terminal);
}

Network::FilterFactoryCb NetworkFilterConfigProviderManagerImpl::instantiateFilterFactory(
    const ProtobufWkt::Any& proto_config, const std::string&,
    Server::Configuration::FactoryContext& factory_context) const {
  auto& factory = Config::Utility::getAndCheckFactoryByType<
      Server::Configuration::NamedNetworkFilterConfigFactory>(proto_config);
  ProtobufTypes::MessagePtr message = Config::Utility::translateAnyToFactoryConfig(
      proto_config, factory_context.messageValidationContext().dynamicValidationVisitor(), factory);
  return factory.createFilterFactoryFromProto(*message, factory_context);
}

void NetworkFilterConfigProviderManagerImpl::validateTypeUrl(const std::string& filter_config_name,
                                                             absl::string_view type_url,
                                                             bool is_terminal) const {
  auto* factory = Registry::FactoryRegistry<
      Server::Configuration::NamedNetworkFilterConfigFactory>::getFactoryByType(type_url);
  if (factory == nullptr) {
    throw EnvoyException(
        fmt::format("Error: no factory found for a required type URL {}.", type_url));
  }
  Config::Utility::validateTerminalFilters(filter_config_name, factory->name(), "network",
                                           factory->isTerminalFilter(), is_terminal);
}

} // namespace Filter
} // namespace Envoy
