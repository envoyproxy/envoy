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
absl::Status validateTypeUrlHelper(const std::string& type_url,
                                   const absl::flat_hash_set<std::string> require_type_urls) {
  if (!require_type_urls.contains(type_url)) {
    return absl::InvalidArgumentError(
        fmt::format("Error: filter config has type URL {} but expect {}.", type_url,
                    absl::StrJoin(require_type_urls, ", ")));
  }
  return absl::OkStatus();
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

absl::Status
DynamicFilterConfigProviderImplBase::validateTypeUrl(const std::string& type_url) const {
  return validateTypeUrlHelper(type_url, require_type_urls_);
}

const std::string& DynamicFilterConfigProviderImplBase::name() { return subscription_->name(); }

absl::StatusOr<std::unique_ptr<FilterConfigSubscription>> FilterConfigSubscription::create(
    const envoy::config::core::v3::ConfigSource& config_source,
    const std::string& filter_config_name,
    Server::Configuration::ServerFactoryContext& factory_context,
    Upstream::ClusterManager& cluster_manager, const std::string& stat_prefix,
    FilterConfigProviderManagerImplBase& filter_config_provider_manager,
    const std::string& subscription_id) {
  absl::Status creation_status = absl::OkStatus();
  auto ret = std::unique_ptr<FilterConfigSubscription>(new FilterConfigSubscription(
      config_source, filter_config_name, factory_context, cluster_manager, stat_prefix,
      filter_config_provider_manager, subscription_id, creation_status));
  RETURN_IF_NOT_OK_REF(creation_status);
  return ret;
}
FilterConfigSubscription::FilterConfigSubscription(
    const envoy::config::core::v3::ConfigSource& config_source,
    const std::string& filter_config_name,
    Server::Configuration::ServerFactoryContext& factory_context,
    Upstream::ClusterManager& cluster_manager, const std::string& stat_prefix,
    FilterConfigProviderManagerImplBase& filter_config_provider_manager,
    const std::string& subscription_id, absl::Status& creation_status)
    : Config::SubscriptionBase<envoy::config::core::v3::TypedExtensionConfig>(
          factory_context.messageValidationContext().dynamicValidationVisitor(), "name"),
      filter_config_name_(filter_config_name),
      last_(std::make_shared<ConfigVersion>("", factory_context.timeSource().systemTime())),
      factory_context_(factory_context),
      init_target_(fmt::format("FilterConfigSubscription init {}", filter_config_name_),
                   [this]() { start(); }),
      scope_(factory_context.scope().createScope(stat_prefix)),
      stats_({ALL_EXTENSION_CONFIG_DISCOVERY_STATS(POOL_COUNTER(*scope_))}),
      filter_config_provider_manager_(filter_config_provider_manager),
      subscription_id_(subscription_id) {
  const auto resource_name = getResourceName();
  auto subscription_or_error = cluster_manager.subscriptionFactory().subscriptionFromConfigSource(
      config_source, Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_, {});
  SET_AND_RETURN_IF_NOT_OK(subscription_or_error.status(), creation_status);
  subscription_ = std::move(*subscription_or_error);
}

void FilterConfigSubscription::start() {
  if (!started_) {
    started_ = true;
    subscription_->start({filter_config_name_});
  }
}

absl::Status
FilterConfigSubscription::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                         const std::string& version_info) {
  ConfigVersionSharedPtr next =
      std::make_shared<ConfigVersion>(version_info, factory_context_.timeSource().systemTime());
  if (resources.size() != 1) {
    return absl::InvalidArgumentError(fmt::format(
        "Unexpected number of resources in ExtensionConfigDS response: {}", resources.size()));
  }
  const auto& filter_config = dynamic_cast<const envoy::config::core::v3::TypedExtensionConfig&>(
      resources[0].get().resource());
  if (filter_config.name() != filter_config_name_) {
    return absl::InvalidArgumentError(fmt::format(
        "Unexpected resource name in ExtensionConfigDS response: {}", filter_config.name()));
  }
  // Skip update if hash matches
  next->config_hash_ = MessageUtil::hash(filter_config.typed_config());
  if (next->config_hash_ == last_->config_hash_) {
    // Initial hash is 0, so this branch happens only after a config was already applied, and
    // there is no need to mark the init target ready.
    return absl::OkStatus();
  }
  // Ensure that the filter config is valid in the filter chain context once the proto is
  // processed. Validation happens before updating to prevent a partial update application. It
  // might be possible that the providers have distinct type URL constraints.
  next->type_url_ = Config::Utility::getFactoryType(filter_config.typed_config());
  for (auto* provider : filter_config_providers_) {
    absl::Status status = provider->validateTypeUrl(next->type_url_);
    if (!status.ok()) {
      return status;
    }
  }
  std::tie(next->config_, next->factory_name_) =
      filter_config_provider_manager_.getMessage(filter_config, factory_context_);
  for (auto* provider : filter_config_providers_) {
    provider->validateMessage(filter_config_name_, *next->config_, next->factory_name_);
  }
  ENVOY_LOG(debug, "Updated filter config {} accepted, posting to workers", filter_config_name_);
  // Update the latest subscription config first, to prevent a race with new
  // providers missing the latest config.
  last_ = std::move(next);
  RETURN_IF_NOT_OK(Common::applyToAllWithCleanup<DynamicFilterConfigProviderImplBase*>(
      filter_config_providers_,
      [last = last_](DynamicFilterConfigProviderImplBase* provider,
                     std::shared_ptr<Cleanup> cleanup) {
        return provider->onConfigUpdate(*last->config_, last->version_info_, [cleanup] {});
      },
      [me = shared_from_this()]() { me->updateComplete(); }));
  // The filter configs are created and published to worker queues at this point, so it
  // is safe to mark the subscription as ready and publish the warmed parent resources.
  ENVOY_LOG(debug, "Updated filter config {} created, warming done", filter_config_name_);
  init_target_.ready();
  return absl::OkStatus();
}

absl::Status FilterConfigSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, const std::string&) {
  if (!removed_resources.empty()) {
    ASSERT(removed_resources.size() == 1);
    ENVOY_LOG(debug, "Removing filter config {}", filter_config_name_);
    last_ = std::make_shared<ConfigVersion>("", factory_context_.timeSource().systemTime());
    RETURN_IF_NOT_OK(Common::applyToAllWithCleanup<DynamicFilterConfigProviderImplBase*>(
        filter_config_providers_,
        [](DynamicFilterConfigProviderImplBase* provider, std::shared_ptr<Cleanup> cleanup) {
          return provider->onConfigRemoved([cleanup] {});
        },
        [me = shared_from_this()]() { me->updateComplete(); }));
  } else if (!added_resources.empty()) {
    ASSERT(added_resources.size() == 1);
    return onConfigUpdate(added_resources, added_resources[0].get().version());
  }
  return absl::OkStatus();
}

void FilterConfigSubscription::onConfigUpdateFailed(Config::ConfigUpdateFailureReason reason,
                                                    const EnvoyException*) {
  ENVOY_LOG(debug, "Updating filter config {} failed due to {}", filter_config_name_,
            static_cast<int>(reason));
  stats_.config_fail_.inc();
  // Make sure to make progress in case the control plane is temporarily failing.
  init_target_.ready();
}

void FilterConfigSubscription::updateComplete() {
  ENVOY_LOG(debug, "Filter config {} worker update complete", filter_config_name_);
  stats_.config_reload_.inc();
}

FilterConfigSubscription::~FilterConfigSubscription() {
  // If we get destroyed during initialization, make sure we signal that we "initialized".
  init_target_.ready();
  // Remove the subscription from the provider manager.
  filter_config_provider_manager_.subscriptions_.erase(subscription_id_);
}

void FilterConfigSubscription::incrementConflictCounter() { stats_.config_conflict_.inc(); }

absl::StatusOr<std::shared_ptr<FilterConfigSubscription>>
FilterConfigProviderManagerImplBase::getSubscription(
    const envoy::config::core::v3::ConfigSource& config_source, const std::string& name,
    Server::Configuration::ServerFactoryContext& server_context,
    Upstream::ClusterManager& cluster_manager, const std::string& stat_prefix) {
  // There are ECDS filters configured. Setup ECDS config dump call backs.
  setupEcdsConfigDumpCallbacks(server_context.admin());

  // FilterConfigSubscriptions are unique based on their config source and filter config name
  // combination.
  // TODO(https://github.com/envoyproxy/envoy/issues/11967) Hash collision can cause subscription
  // aliasing.
  const std::string subscription_id = absl::StrCat(MessageUtil::hash(config_source), ".", name);
  auto it = subscriptions_.find(subscription_id);
  if (it == subscriptions_.end()) {
    auto subscription_or_error = FilterConfigSubscription::create(
        config_source, name, server_context, cluster_manager, stat_prefix, *this, subscription_id);
    RETURN_IF_NOT_OK(subscription_or_error.status());
    std::shared_ptr<FilterConfigSubscription> subscription = std::move(*subscription_or_error);
    subscriptions_.insert({subscription_id, std::weak_ptr<FilterConfigSubscription>(subscription)});
    return subscription;
  } else {
    auto existing = it->second.lock();
    ASSERT(existing != nullptr,
           absl::StrCat("Cannot find subscribed filter config resource ", name));
    return existing;
  }
}

absl::Status FilterConfigProviderManagerImplBase::applyLastOrDefaultConfig(
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
      absl::Status status = provider.validateTypeUrl(subscription->lastTypeUrl());
      if (status.ok()) {
        provider.validateMessage(filter_config_name, *subscription->lastConfig(),
                                 subscription->lastFactoryName());
        last_config_valid = true;
      } else {
        ENVOY_LOG(debug, "ECDS subscription {} is invalid in a listener context: {}.",
                  filter_config_name, status.message());
        subscription->incrementConflictCounter();
      }
    }
    END_TRY CATCH(const EnvoyException& e, {
      ENVOY_LOG(debug, "ECDS subscription {} is invalid in a listener context: {}.",
                filter_config_name, e.what());
      subscription->incrementConflictCounter();
    });

    if (last_config_valid) {
      RETURN_IF_NOT_OK(provider.onConfigUpdate(*subscription->lastConfig(),
                                               subscription->lastVersionInfo(), nullptr));
    }
  }

  // Apply the default config if none has been applied.
  if (!last_config_valid) {
    RETURN_IF_NOT_OK(provider.applyDefaultConfiguration());
  }
  return absl::OkStatus();
}

absl::Status FilterConfigProviderManagerImplBase::validateProtoConfigDefaultFactory(
    const bool null_default_factory, const std::string& filter_config_name,
    absl::string_view type_url) const {
  if (null_default_factory) {
    return absl::InvalidArgumentError(
        fmt::format("Error: cannot find filter factory {} for default filter "
                    "configuration with type URL {}.",
                    filter_config_name, type_url));
  }
  return absl::OkStatus();
}

absl::Status FilterConfigProviderManagerImplBase::validateProtoConfigTypeUrl(
    const std::string& type_url, const absl::flat_hash_set<std::string>& require_type_urls) const {
  return validateTypeUrlHelper(type_url, require_type_urls);
}

ProtobufTypes::MessagePtr FilterConfigProviderManagerImplBase::dumpEcdsFilterConfigs(
    const Matchers::StringMatcher& name_matcher) {
  auto config_dump = std::make_unique<envoy::admin::v3::EcdsConfigDump>();
  for (const auto& subscription : subscriptions_) {
    const auto& ecds_filter = subscription.second.lock();
    if (!ecds_filter || !ecds_filter->lastConfig() || !name_matcher.match(ecds_filter->name())) {
      continue;
    }
    envoy::config::core::v3::TypedExtensionConfig filter_config;
    filter_config.set_name(ecds_filter->name());
    MessageUtil::packFrom(*filter_config.mutable_typed_config(), *ecds_filter->lastConfig());
    auto& filter_config_dump = *config_dump->mutable_ecds_filters()->Add();
    filter_config_dump.mutable_ecds_filter()->PackFrom(filter_config);
    filter_config_dump.set_version_info(ecds_filter->lastVersionInfo());
    TimestampUtil::systemClockToTimestamp(ecds_filter->lastUpdated(),
                                          *(filter_config_dump.mutable_last_updated()));
  }
  return config_dump;
}

} // namespace Filter
} // namespace Envoy
