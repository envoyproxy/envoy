#include "source/extensions/filters/common/local_ratelimit/provider_singleton.h"

#include "source/common/grpc/common.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

SINGLETON_MANAGER_REGISTRATION(local_ratelimit);

std::shared_ptr<LocalRateLimiterImpl>
createRateLimiterImpl(const envoy::type::v3::TokenBucket& token_bucket,
                      Event::Dispatcher& dispatcher) {
  const auto fill_interval =
      std::chrono::milliseconds(PROTOBUF_GET_MS_REQUIRED(token_bucket, fill_interval));
  const auto max_tokens = token_bucket.max_tokens();
  const auto tokens_per_fill = PROTOBUF_GET_WRAPPED_OR_DEFAULT(token_bucket, tokens_per_fill, 1);
  return std::make_shared<LocalRateLimiterImpl>(
      fill_interval, max_tokens, tokens_per_fill, dispatcher,
      Protobuf::RepeatedPtrField<
          envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>());
}

RateLimiterProviderSingleton::RateLimiterWrapperPtr RateLimiterProviderSingleton::getRateLimiter(
    Server::Configuration::ServerFactoryContext& factory_context, absl::string_view key,
    const envoy::config::core::v3::ConfigSource& config_source, SetRateLimiterCb callback) {
  RateLimiterProviderSingletonSharedPtr provider =
      factory_context.singletonManager().getTyped<RateLimiterProviderSingleton>(
          SINGLETON_MANAGER_REGISTERED_NAME(local_ratelimit), [&factory_context, &config_source] {
            return std::make_shared<RateLimiterProviderSingleton>(factory_context, config_source);
          });
  auto it = provider->limiters_.find(key);
  if (it != provider->limiters_.end()) {
    auto& limiter_ref = it->second;
    auto limiter = limiter_ref.limiter().lock();
    if (limiter) {
      // The limiter is still alive, return the wrapper with the limiter.
      return std::make_unique<RateLimiterProviderSingleton::RateLimiterWrapper>(provider, limiter);
    }

    // The limiter doesn't exist but the config is still there, create a new one
    // and return the wrapper with the new limiter.
    const auto& named_token_bucket = limiter_ref.config();
    limiter = createRateLimiterImpl(named_token_bucket.token_bucket(),
                                    provider->factory_context_.mainThreadDispatcher());
    limiter_ref.setLimiter(limiter);
    return std::make_unique<RateLimiterProviderSingleton::RateLimiterWrapper>(provider, limiter);
  }

  // The limiter doesn't exist and the config doesn't exist either, we need to
  // wait for the config to be ready. Add the callback to the list of callbacks
  // for the config.
  auto callback_it = provider->setter_cbs_.find(key);
  if (callback_it == provider->setter_cbs_.end()) {
    callback_it = provider->setter_cbs_.emplace(key, factory_context).first;
  }
  callback_it->second.addCallback(std::move(callback));

  return std::make_unique<RateLimiterProviderSingleton::RateLimiterWrapper>(provider, nullptr);
}

// Definition of RateLimitConfigCallback methods
RateLimiterProviderSingleton::RateLimitConfigCallback::RateLimitConfigCallback(
    Server::Configuration::ServerFactoryContext& factory_context)
    : init_target_(std::make_unique<Init::TargetImpl>("RateLimitConfigCallback", []() {})) {
  factory_context.initManager().add(*init_target_);
}

void RateLimiterProviderSingleton::RateLimitConfigCallback::addCallback(SetRateLimiterCb callback) {
  setters_.push_back(std::move(callback));
}

void RateLimiterProviderSingleton::RateLimitConfigCallback::setLimiter(
    std::shared_ptr<LocalRateLimiterImpl> limiter) {
  for (auto& setter : setters_) {
    setter(limiter);
  }
  // Mark ready only once.
  if (!init_target_->ready()) {
    init_target_->ready();
  }
}

RateLimiterProviderSingleton::RatelimiterSubscription::RatelimiterSubscription(
    RateLimiterProviderSingleton& parent)
    : Config::SubscriptionBase<envoy::type::v3::TokenBucketConfig>(
          parent.factory_context_.messageValidationVisitor(), "name"),
      parent_(parent) {
  subscription_ = THROW_OR_RETURN_VALUE(
      parent.factory_context_.clusterManager().subscriptionFactory().subscriptionFromConfigSource(
          parent.config_source_, Grpc::Common::typeUrl(getResourceName()), *parent.scope_, *this,
          resource_decoder_, {}),
      Config::SubscriptionPtr);
}

void RateLimiterProviderSingleton::RatelimiterSubscription::handleAddedResources(
    const std::vector<Config::DecodedResourceRef>& added_resources) {
  for (const auto& resource : added_resources) {
    std::string key = resource.get().name();
    const auto& named_token_bucket =
        dynamic_cast<const envoy::type::v3::TokenBucketConfig&>(resource.get().resource());

    auto it = parent_.limiters_.find(key);
    if (it == parent_.limiters_.end()) {
      parent_.limiters_.emplace(
          key, RateLimiterRef(named_token_bucket, std::weak_ptr<LocalRateLimiterImpl>()));
    } else {
      // The config may be different from the existing one. We need to update the
      // config and reset the limiter if it is.
      it->second.setConfig(named_token_bucket);
    }

    auto callback_it = parent_.setter_cbs_.find(key);
    if (callback_it == parent_.setter_cbs_.end()) {
      continue; // No pending callbacks for this key.
    }

    auto limiter = createRateLimiterImpl(named_token_bucket.token_bucket(),
                                         parent_.factory_context_.mainThreadDispatcher());
    parent_.limiters_.at(key).setLimiter(limiter);
    callback_it->second.setLimiter(limiter); // Notify all waiting callbacks.
    parent_.setter_cbs_.erase(callback_it);
  }
}

void RateLimiterProviderSingleton::RatelimiterSubscription::handleRemovedResources(
    const Protobuf::RepeatedPtrField<std::string>& removed_resources) {
  for (const auto& key : removed_resources) {
    auto it = parent_.limiters_.find(key);
    if (it != parent_.limiters_.end()) {
      // The wrapper containing the config and the weak_ptr is removed.
      // Any existing shared_ptr instances held by RateLimiter objects
      // will keep the LocalRateLimiterImpl alive until they are destroyed.
      parent_.limiters_.erase(it);
    }
  }
}

absl::Status RateLimiterProviderSingleton::RatelimiterSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& resources, const std::string&) {
  // SOTW update. We need to determine what was removed.
  absl::flat_hash_set<std::string> new_keys;
  for (const auto& res : resources) {
    new_keys.insert(res.get().name());
  }

  Protobuf::RepeatedPtrField<std::string> removed_resources;
  for (const auto& pair : parent_.limiters_) {
    if (!new_keys.contains(pair.first)) {
      *removed_resources.Add() = pair.first;
    }
  }

  handleAddedResources(resources);
  handleRemovedResources(removed_resources);
  return absl::OkStatus();
}

absl::Status RateLimiterProviderSingleton::RatelimiterSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, const std::string&) {
  handleAddedResources(added_resources);
  handleRemovedResources(removed_resources);
  return absl::OkStatus();
}

} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
