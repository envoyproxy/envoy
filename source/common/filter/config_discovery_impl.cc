#include "source/common/filter/config_discovery_impl.h"

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/config/extension_config_provider.h"
#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/containers.h"
#include "source/common/common/thread.h"
#include "source/common/grpc/common.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Filter {

namespace {
void validateTypeUrlHelper(const std::string& type_url,
                           const absl::flat_hash_set<std::string> require_type_urls) {
  if (!require_type_urls.contains(type_url)) {
    throw EnvoyException(fmt::format("Error: filter config has type URL {} but expect {}.",
                                     type_url, absl::StrJoin(require_type_urls, ", ")));
  }
}

} // namespace

DynamicFilterConfigProviderImplBase::DynamicFilterConfigProviderImplBase(
    FilterConfigSubscriptionSharedPtr& subscription,
    const absl::flat_hash_set<std::string>& require_type_urls, bool last_filter_in_filter_chain,
    const std::string& filter_chain_type)
    : subscription_(subscription), require_type_urls_(require_type_urls),
      init_target_("DynamicFilterConfigProviderImpl",
                   [this]() {
                     subscription_->start();
                     // This init target is used to activate the subscription but not wait for a
                     // response. It is used whenever a default config is provided to be used while
                     // waiting for a response.
                     init_target_.ready();
                   }),
      last_filter_in_filter_chain_(last_filter_in_filter_chain),
      filter_chain_type_(filter_chain_type) {

  subscription_->filter_config_providers_.insert(this);
}

DynamicFilterConfigProviderImplBase::~DynamicFilterConfigProviderImplBase() {
  subscription_->filter_config_providers_.erase(this);
}

void DynamicFilterConfigProviderImplBase::validateTypeUrl(const std::string& type_url) const {
  validateTypeUrlHelper(type_url, require_type_urls_);
}

const std::string& DynamicFilterConfigProviderImplBase::name() { return subscription_->name(); }

FilterConfigSubscription::FilterConfigSubscription(
    const envoy::config::core::v3::ConfigSource& config_source,
    const std::string& filter_config_name,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    FilterConfigProviderManagerImplBase& filter_config_provider_manager,
    const std::string& subscription_id)
    : Config::SubscriptionBase<envoy::config::core::v3::TypedExtensionConfig>(
          factory_context.messageValidationContext().dynamicValidationVisitor(), "name"),
      filter_config_name_(filter_config_name), factory_context_(factory_context),
      init_target_(fmt::format("FilterConfigSubscription init {}", filter_config_name_),
                   [this]() { start(); }),
      scope_(factory_context.scope().createScope(stat_prefix)),
      stats_({ALL_EXTENSION_CONFIG_DISCOVERY_STATS(POOL_COUNTER(*scope_))}),
      filter_config_provider_manager_(filter_config_provider_manager),
      subscription_id_(subscription_id) {
  const auto resource_name = getResourceName();
  subscription_ =
      factory_context.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          config_source, Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_,
          {});
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
  const auto type_url = Config::Utility::getFactoryType(filter_config.typed_config());
  for (auto* provider : filter_config_providers_) {
    provider->validateTypeUrl(type_url);
  }
  auto [message, factory_name] =
      filter_config_provider_manager_.getMessage(filter_config, factory_context_);
  for (auto* provider : filter_config_providers_) {
    provider->validateMessage(filter_config_name_, *message, factory_name);
  }
  ENVOY_LOG(debug, "Updating filter config {}", filter_config_name_);

  Common::applyToAllWithCleanup<DynamicFilterConfigProviderImplBase*>(
      filter_config_providers_,
      [&message = message, &version_info](DynamicFilterConfigProviderImplBase* provider,
                                          std::shared_ptr<Cleanup> cleanup) {
        provider->onConfigUpdate(*message, version_info, [cleanup] {});
      },
      [this]() { stats_.config_reload_.inc(); });
  last_config_hash_ = new_hash;
  last_config_ = std::move(message);
  last_type_url_ = type_url;
  last_version_info_ = version_info;
  last_factory_name_ = factory_name;
}

void FilterConfigSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, const std::string&) {
  if (!removed_resources.empty()) {
    ASSERT(removed_resources.size() == 1);
    ENVOY_LOG(debug, "Removing filter config {}", filter_config_name_);
    Common::applyToAllWithCleanup<DynamicFilterConfigProviderImplBase*>(
        filter_config_providers_,
        [](DynamicFilterConfigProviderImplBase* provider, std::shared_ptr<Cleanup> cleanup) {
          provider->onConfigRemoved([cleanup] {});
        },
        [this]() { stats_.config_reload_.inc(); });

    last_config_hash_ = 0;
    last_config_ = nullptr;
    last_type_url_ = "";
    last_version_info_ = "";
    last_factory_name_ = "";
  } else if (!added_resources.empty()) {
    onConfigUpdate(added_resources, added_resources[0].get().version());
  }
}

void FilterConfigSubscription::onConfigUpdateFailed(Config::ConfigUpdateFailureReason reason,
                                                    const EnvoyException*) {
  ENVOY_LOG(debug, "Updating filter config {} failed due to {}", filter_config_name_,
            static_cast<int>(reason));
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

void FilterConfigSubscription::incrementConflictCounter() { stats_.config_conflict_.inc(); }

std::shared_ptr<FilterConfigSubscription> FilterConfigProviderManagerImplBase::getSubscription(
    const envoy::config::core::v3::ConfigSource& config_source, const std::string& name,
    Server::Configuration::ServerFactoryContext& server_context, const std::string& stat_prefix) {
  // FilterConfigSubscriptions are unique based on their config source and filter config name
  // combination.
  // TODO(https://github.com/envoyproxy/envoy/issues/11967) Hash collision can cause subscription
  // aliasing.
  const std::string subscription_id = absl::StrCat(MessageUtil::hash(config_source), ".", name);
  auto it = subscriptions_.find(subscription_id);
  if (it == subscriptions_.end()) {
    auto subscription = std::make_shared<FilterConfigSubscription>(
        config_source, name, server_context, stat_prefix, *this, subscription_id);
    subscriptions_.insert({subscription_id, std::weak_ptr<FilterConfigSubscription>(subscription)});
    return subscription;
  } else {
    auto existing = it->second.lock();
    ASSERT(existing != nullptr,
           absl::StrCat("Cannot find subscribed filter config resource ", name));
    return existing;
  }
}

void FilterConfigProviderManagerImplBase::applyLastOrDefaultConfig(
    std::shared_ptr<FilterConfigSubscription>& subscription,
    DynamicFilterConfigProviderImplBase& provider, const std::string& filter_config_name) {
  // If the subscription already received a config, attempt to apply it.
  // It is possible that the received extension config fails to satisfy the listener
  // type URL constraints. This may happen if ECDS and LDS updates are racing, and the LDS
  // update arrives first. In this case, use the default config, increment a metric,
  // and the applied config eventually converges once ECDS update arrives.
  bool last_config_valid = false;
  if (subscription->lastConfig()) {
    TRY_ASSERT_MAIN_THREAD {
      provider.validateTypeUrl(subscription->lastTypeUrl());
      provider.validateMessage(filter_config_name, *subscription->lastConfig(),
                               subscription->lastFactoryName());
      last_config_valid = true;
    }
    END_TRY catch (const EnvoyException& e) {
      ENVOY_LOG(debug, "ECDS subscription {} is invalid in a listener context: {}.",
                filter_config_name, e.what());
      subscription->incrementConflictCounter();
    }
    if (last_config_valid) {
      provider.onConfigUpdate(*subscription->lastConfig(), subscription->lastVersionInfo(),
                              nullptr);
    }
  }

  // Apply the default config if none has been applied.
  if (!last_config_valid) {
    provider.applyDefaultConfiguration();
  }
}

void FilterConfigProviderManagerImplBase::validateProtoConfigDefaultFactory(
    const bool null_default_factory, const std::string& filter_config_name,
    absl::string_view type_url) const {
  if (null_default_factory) {
    throw EnvoyException(fmt::format("Error: cannot find filter factory {} for default filter "
                                     "configuration with type URL {}.",
                                     filter_config_name, type_url));
  }
}

void FilterConfigProviderManagerImplBase::validateProtoConfigTypeUrl(
    const std::string& type_url, const absl::flat_hash_set<std::string>& require_type_urls) const {
  validateTypeUrlHelper(type_url, require_type_urls);
}

} // namespace Filter
} // namespace Envoy
