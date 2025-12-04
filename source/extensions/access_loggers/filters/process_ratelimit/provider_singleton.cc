#include "source/extensions/access_loggers/filters/process_ratelimit/provider_singleton.h"

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

void RateLimiterProviderSingleton::RateLimiterWrapper::setLimiter(
    LocalRateLimiterSharedPtr limiter) {
  ENVOY_BUG(init_target_ != nullptr,
            "init_target_ should not be null if the limiter is set from callback");
  limiter_slot_.runOnAllThreads(
      [limiter, cancelled = cancelled_](OptRef<ThreadLocalLimiter> thread_local_limiter) {
        if (!cancelled->load()) {
          thread_local_limiter->limiter = limiter;
        }
      },
      [init_target = init_target_.get(), cancelled = cancelled_]() {
        if (!cancelled->load()) {
          init_target->ready();
        }
      });
}

RateLimiterProviderSingleton::RateLimiterWrapperPtr RateLimiterProviderSingleton::getRateLimiter(
    Server::Configuration::ServerFactoryContext& factory_context, absl::string_view key,
    const envoy::config::core::v3::ConfigSource& config_source, intptr_t setter_key,
    SetRateLimiterCb setter) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  auto provider = factory_context.singletonManager().getTyped<RateLimiterProviderSingleton>(
      SINGLETON_MANAGER_REGISTERED_NAME(local_ratelimit_provider),
      [&factory_context, &config_source] {
        return std::make_shared<RateLimiterProviderSingleton>(factory_context, config_source);
      });

  // Find the subscription for the given key.
  auto it = provider->subscriptions_.find(key);
  TokenBucketSubscriptionSharedPtr subscription = nullptr;
  if (it == provider->subscriptions_.end()) {
    // If the subscription doesn't exist, create a new one.
    subscription = std::make_shared<TokenBucketSubscription>(*provider, key);
    it = provider->subscriptions_.emplace(key, subscription).first;
  } else {
    auto exist_subscription = it->second.lock();
    ENVOY_BUG(exist_subscription != nullptr,
              fmt::format("subscription for {} should not be null since it should be "
                          "cleaned up when the last wrapper is destroyed",
                          key));
    subscription = exist_subscription;
  }
  subscription->addSetter(setter_key, std::move(setter));

  // If the limiter is already created, return it.
  if (auto limiter = subscription->getLimiter()) {
    return std::make_unique<RateLimiterWrapper>(factory_context.threadLocal(), provider,
                                                subscription, limiter, nullptr);
  }

  auto init_target =
      std::make_unique<Init::TargetImpl>(fmt::format("RateLimitConfigCallback-{}", key), []() {});

  // Add the init target to the listener's init manager to wait for the
  // resource.
  factory_context.initManager().add(*init_target);

  // Otherwise, return a wrapper with a null limiter. The limiter will be
  // set when the config is received.
  return std::make_unique<RateLimiterWrapper>(factory_context.threadLocal(), provider, subscription,
                                              nullptr, std::move(init_target));
}

std::shared_ptr<LocalRateLimiterImpl>
RateLimiterProviderSingleton::TokenBucketSubscription::getLimiter() {
  auto limiter = limiter_.lock();
  if (limiter) {
    return limiter;
  }
  if (config_.has_value()) {
    limiter =
        createRateLimiterImpl(config_.value(), parent_.factory_context_.mainThreadDispatcher());
    limiter_ = limiter;
    return limiter;
  }
  return nullptr;
}

RateLimiterProviderSingleton::TokenBucketSubscription::TokenBucketSubscription(
    RateLimiterProviderSingleton& parent, absl::string_view resource_name)
    : Config::SubscriptionBase<envoy::type::v3::TokenBucket>(
          parent.factory_context_.messageValidationVisitor(), ""),
      parent_(parent), resource_name_(resource_name), token_bucket_config_hash_(0) {
  subscription_ = THROW_OR_RETURN_VALUE(
      parent.factory_context_.xdsManager().subscribeToSingletonResource(
          resource_name, parent.config_source_, Grpc::Common::typeUrl(getResourceName()),
          *parent.scope_, *this, resource_decoder_, {}),
      Config::SubscriptionPtr);
  subscription_->start({resource_name_});
}

RateLimiterProviderSingleton::TokenBucketSubscription::~TokenBucketSubscription() {
  parent_.subscriptions_.erase(resource_name_);
}

void RateLimiterProviderSingleton::TokenBucketSubscription::handleAddedResource(
    const Config::DecodedResourceRef& resource) {
  const auto& config = dynamic_cast<const envoy::type::v3::TokenBucket&>(resource.get().resource());
  size_t new_hash = MessageUtil::hash(config);
  // If the config is the same, no op.
  if (new_hash == token_bucket_config_hash_) {
    return;
  }

  // Update the config and hash and reset the limiter.
  config_ = config;
  token_bucket_config_hash_ = new_hash;
  auto new_limiter = createRateLimiterImpl(config, parent_.factory_context_.mainThreadDispatcher());
  limiter_ = new_limiter;
  for (auto& [key, setter] : setters_) {
    // The method is called on main thread while the limiter will be accessed in the worker thread
    // so setter will call `runOnAllThreads` to set underneath.
    setter(new_limiter);
  }
}

void RateLimiterProviderSingleton::TokenBucketSubscription::handleRemovedResource(
    absl::string_view) {
  // We simply reset the config and limiter here. The existing rate limiter will
  // continue to work before the new config is received.
  config_.reset();
  token_bucket_config_hash_ = 0;
  limiter_.reset();

  // Reset the init target as we are now waiting for a new resource.
  for (auto& [key, setter] : setters_) {
    setter(parent_.fallback_always_deny_limiter_);
  }
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
