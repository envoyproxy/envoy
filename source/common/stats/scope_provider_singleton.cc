#include "source/common/stats/scope_provider_singleton.h"

#include "source/common/grpc/common.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Stats {

namespace {

struct ScopeConfiguration {
  ScopeStatsLimitSettings limits;
  bool evictable;
};

ScopeConfiguration
convertProtoToScopeStatsLimitSettings(const envoy::config::metrics::v3::Scope& config) {
  ScopeStatsLimitSettings limits;
  limits.max_counters = config.max_counters();
  limits.max_gauges = config.max_gauges();
  limits.max_histograms = config.max_histograms();
  return {limits, config.evictable()};
}

} // namespace

SINGLETON_MANAGER_REGISTRATION(scope_provider);

ScopeProviderSingleton::ScopeWrapper::~ScopeWrapper() {
  if (subscription_) {
    subscription_->removeSetter(reinterpret_cast<intptr_t>(this));
  }
}

ScopeProviderSingleton::ScopeWrapperPtr ScopeProviderSingleton::getScopeWrapper(
    Server::Configuration::GenericFactoryContext& factory_context, absl::string_view key,
    const envoy::config::core::v3::ConfigSource& config_source) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  auto provider =
      factory_context.serverFactoryContext().singletonManager().getTyped<ScopeProviderSingleton>(
          SINGLETON_MANAGER_REGISTERED_NAME(scope_provider), [&factory_context, &config_source] {
            return std::make_shared<ScopeProviderSingleton>(factory_context.serverFactoryContext(),
                                                            config_source);
          });

  // Find the subscription for the given key.
  auto it = provider->subscriptions_.find(key);
  ScopeSubscriptionSharedPtr subscription = nullptr;
  if (it == provider->subscriptions_.end()) {
    subscription = std::make_shared<ScopeSubscription>(*provider, key);
    it = provider->subscriptions_.emplace(key, subscription).first;
  } else {
    auto exist_subscription = it->second.lock();
    if (exist_subscription == nullptr) {
      subscription = std::make_shared<ScopeSubscription>(*provider, key);
      it->second = subscription;
    } else {
      subscription = exist_subscription;
    }
  }

  ScopeSharedPtr scope = subscription->getScope();
  std::unique_ptr<Init::TargetImpl> init_target;

  if (!scope) {
    init_target =
        std::make_unique<Init::TargetImpl>(fmt::format("ScopeConfigCallback-{}", key), []() {});
    factory_context.initManager().add(*init_target);
  }

  auto wrapper =
      std::make_unique<ScopeWrapper>(provider, subscription, scope, std::move(init_target));

  // Register setter to notify init target ready.
  subscription->addSetter(
      reinterpret_cast<intptr_t>(wrapper.get()),
      [w = wrapper.get()](ScopeSharedPtr new_scope) { w->setScope(new_scope); });

  return wrapper;
}

ScopeProviderSingleton::ScopeSubscription::ScopeSubscription(ScopeProviderSingleton& parent,
                                                             absl::string_view resource_name)
    : Config::SubscriptionBase<envoy::config::metrics::v3::Scope>(
          parent.factory_context_.messageValidationVisitor(), ""),
      parent_(parent), resource_name_(resource_name),
      fallback_scope_(parent.factory_context_.scope().createScope(resource_name_)) {

  // Subscription start
  // We need to use xDS manager.
  subscription_ = THROW_OR_RETURN_VALUE(
      parent.factory_context_.xdsManager().subscribeToSingletonResource(
          resource_name, parent.config_source_, Grpc::Common::typeUrl(getResourceName()),
          *parent.scope_, *this, resource_decoder_, {}),
      Config::SubscriptionPtr);
  subscription_->start({resource_name_});
}

ScopeProviderSingleton::ScopeSubscription::~ScopeSubscription() {
  parent_.subscriptions_.erase(resource_name_);
}

void ScopeProviderSingleton::ScopeSubscription::handleAddedResource(
    const Config::DecodedResourceRef& resource) {
  const auto& config =
      dynamic_cast<const envoy::config::metrics::v3::Scope&>(resource.get().resource());
  size_t new_hash = MessageUtil::hash(config);

  if (new_hash == config_hash_) {
    return;
  }

  config_hash_ = new_hash;
  auto scope_config = convertProtoToScopeStatsLimitSettings(config);

  // Create new scope with limits.
  // Note: createScope() handles overlapping scopes that point to the same reference counted
  // backing stats. This allows a new scope to be gracefully swapped in while an old scope
  // with the same name is being destroyed.
  scope_ = parent_.factory_context_.scope().createScope(resource_name_, scope_config.evictable,
                                                        scope_config.limits);

  for (auto& [key, setter] : setters_) {
    setter(scope_);
  }
}

void ScopeProviderSingleton::ScopeSubscription::handleRemovedResource(absl::string_view) {
  // Revert to default scope (no limits).
  // We use the fallback scope that was created with default settings.
  scope_ = fallback_scope_;

  for (auto& [key, setter] : setters_) {
    setter(scope_);
  }
}

absl::Status ScopeProviderSingleton::ScopeSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& resources, const std::string&) {
  if (resources.size() != 1) {
    return absl::InvalidArgumentError(
        fmt::format("for singleton resource subscription, only one resource should be "
                    "added or removed at a time but got {}",
                    resources.size()));
  }
  if (resources[0].get().name() != resource_name_) {
    return absl::InvalidArgumentError(
        fmt::format("Unexpected resource name: {}", resources[0].get().name()));
  }

  handleAddedResource(resources[0]);
  return absl::OkStatus();
}

absl::Status ScopeProviderSingleton::ScopeSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, const std::string&) {

  if (added_resources.size() + removed_resources.size() != 1) {
    return absl::InvalidArgumentError(
        fmt::format("Update must contain exactly one add or remove, got {} adds and {} removes",
                    added_resources.size(), removed_resources.size()));
  }

  if (added_resources.size() == 1) {
    if (added_resources[0].get().name() != resource_name_) {
      return absl::InvalidArgumentError(
          fmt::format("Unexpected resource name: {}", added_resources[0].get().name()));
    }
    handleAddedResource(added_resources[0]);
  } else {
    if (removed_resources[0] != resource_name_) {
      return absl::InvalidArgumentError(
          fmt::format("Unexpected removed resource name: {}", removed_resources[0]));
    }
    handleRemovedResource(removed_resources[0]);
  }

  return absl::OkStatus();
}

} // namespace Stats
} // namespace Envoy
