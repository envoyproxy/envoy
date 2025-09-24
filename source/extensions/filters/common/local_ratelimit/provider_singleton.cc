#include "source/extensions/filters/common/local_ratelimit/provider_singleton.h"

#include "source/common/grpc/common.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace LocalRateLimit {

SINGLETON_MANAGER_REGISTRATION(local_ratelimit_provider);

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
  auto provider = factory_context.singletonManager().getTyped<RateLimiterProviderSingleton>(
      SINGLETON_MANAGER_REGISTERED_NAME(local_ratelimit_provider),
      [&factory_context, &config_source] {
        return std::make_shared<RateLimiterProviderSingleton>(factory_context, config_source);
      });

  // Find the subscription for the given key.
  auto it = provider->subscriptions_.find(key);
  if (it == provider->subscriptions_.end()) {
    // If the subscription doesn't exist, create a new one.
    auto subscription = std::make_unique<TokenBucketSubscription>(*provider, key);
    it = provider->subscriptions_.emplace(key, std::move(subscription)).first;
  }

  auto& subscription = it->second;

  // If the limiter is already created, return it.
  if (auto limiter = subscription->getLimiter()) {
    return std::make_unique<RateLimiterWrapper>(provider, limiter);
  }

  // Otherwise, return a wrapper with a null limiter. The limiter will be
  // set when the config is received.
  subscription->addSetter(std::move(callback));
  return std::make_unique<RateLimiterWrapper>(provider, nullptr);
}

std::shared_ptr<LocalRateLimiterImpl>
RateLimiterProviderSingleton::TokenBucketSubscription::getLimiter() {
  auto limiter = limiter_.lock();
  if (limiter) {
    return limiter;
  }
  if (config_.has_value()) {
    limiter = createRateLimiterImpl(config_->token_bucket(),
                                    parent_.factory_context_.mainThreadDispatcher());
    limiter_ = limiter;
    return limiter;
  }
  return nullptr;
}

RateLimiterProviderSingleton::TokenBucketSubscription::TokenBucketSubscription(
    RateLimiterProviderSingleton& parent, absl::string_view resource_name)
    : Config::SubscriptionBase<envoy::type::v3::TokenBucketConfig>(
          parent.factory_context_.messageValidationVisitor(), "name"),
      parent_(parent),
      init_target_(std::make_unique<Init::TargetImpl>("RateLimitConfigCallback", []() {})),
      resource_name_(resource_name), token_bucket_config_hash_(0) {
  parent_.factory_context_.initManager().add(*init_target_);
  subscription_ = THROW_OR_RETURN_VALUE(
      parent.factory_context_.xdsManager().subscribeToSingletonResource(
          resource_name, parent.config_source_, Grpc::Common::typeUrl(getResourceName()),
          *parent.scope_, *this, resource_decoder_, {}),
      Config::SubscriptionPtr);
  subscription_->start({resource_name_});
}

void RateLimiterProviderSingleton::TokenBucketSubscription::handleAddedResource(
    const Config::DecodedResourceRef& resource) {
  const auto& config =
      dynamic_cast<const envoy::type::v3::TokenBucketConfig&>(resource.get().resource());
  // Update the config.
  size_t new_hash = MessageUtil::hash(config);
  // If the config is the same, no op.
  if (new_hash == token_bucket_config_hash_) {
    return;
  }
  config_ = config;
  token_bucket_config_hash_ = new_hash;
  limiter_.reset();

  // If limiter is not created, create a new limiter and set it.
  auto limiter = limiter_.lock();
  if (limiter == nullptr) {
    limiter = createRateLimiterImpl(config.token_bucket(),
                                    parent_.factory_context_.mainThreadDispatcher());
  }

  limiter_ = limiter;
  for (auto& setter : setters_) {
    setter(limiter);
  }

  // Mark ready only once.
  init_target_->ready();
}

void RateLimiterProviderSingleton::TokenBucketSubscription::handleRemovedResource(
    absl::string_view resource) {
  parent_.subscriptions_.erase(resource);
}

absl::Status RateLimiterProviderSingleton::TokenBucketSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& resources, const std::string&) {
  ENVOY_BUG(resources.size() == 1,
            fmt::format("for singleton resource subscription, only one resource should be "
                        "added or removed at a time but got {}",
                        resources.size()));
  ENVOY_BUG(resources[0].get().name() == resource_name_,
            fmt::format("for singleton resource subscription, the added resource name "
                        "should be the same as the subscription resource name but got "
                        "{} != {}",
                        resources[0].get().name(), resource_name_));

  handleAddedResource(resources[0]);
  return absl::OkStatus();
}

absl::Status RateLimiterProviderSingleton::TokenBucketSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, const std::string&) {
  ENVOY_BUG(added_resources.size() + removed_resources.size() == 1,
            fmt::format("for singleton resource subscription, only one resource should be "
                        "added or removed at a time but got {}",
                        added_resources.size() + removed_resources.size()));
  if (added_resources.size() == 1) {
    ENVOY_BUG(added_resources[0].get().name() == resource_name_,
              fmt::format("for singleton resource subscription, the added resource name "
                          "should be the same as the subscription resource name but got {} "
                          "!= {}",
                          added_resources[0].get().name(), resource_name_));
    handleAddedResource(added_resources[0]);
  } else {
    ENVOY_BUG(removed_resources[0] == resource_name_,
              fmt::format("for singleton resource subscription, the removed resource name "
                          "should be the same as the subscription resource name but got {} "
                          "!= {}",
                          removed_resources[0], resource_name_));
    handleRemovedResource(removed_resources[0]);
  }

  return absl::OkStatus();
}
} // namespace LocalRateLimit
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
