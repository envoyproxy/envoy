#include "source/extensions/filters/network/local_ratelimit/local_ratelimit.h"

#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LocalRateLimitFilter {

SINGLETON_MANAGER_REGISTRATION(shared_local_ratelimit);

SharedRateLimitSingleton::~SharedRateLimitSingleton() {
  // Validate that all limiters were properly cleaned up when Configs are deleted.
  ASSERT(limiters_.empty());
}

std::pair<LocalRateLimiterImplSharedPtr, const SharedRateLimitSingleton::Key*>
SharedRateLimitSingleton::get(
    const envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit& proto_config,
    std::function<LocalRateLimiterImplSharedPtr()> create_fn) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  if (proto_config.share_key().empty()) {
    // If there is no key, never share.
    return {create_fn(), nullptr};
  }

  LocalRateLimiterImplSharedPtr rate_limiter;
  const Key key{proto_config.share_key(), proto_config.token_bucket()};
  auto it = limiters_.find(key);
  if (it == limiters_.end()) {
    ENVOY_LOG(debug, fmt::format("LocalRateLimit for share_key '{}' is creating a new token bucket "
                                 "due to no match found",
                                 proto_config.share_key()));
    rate_limiter = create_fn();
    it = limiters_.emplace(key, rate_limiter).first;
  } else {
    ENVOY_LOG(
        debug,
        fmt::format("LocalRateLimit for share_key '{}' is using an existing matching token bucket",
                    proto_config.share_key()));
    rate_limiter = it->second.lock();
    ASSERT(rate_limiter != nullptr);
  }
  return {rate_limiter, &it->first};
}

void SharedRateLimitSingleton::removeIfUnused(const Key* key) {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (key != nullptr) {
    auto it = limiters_.find(*key);
    ASSERT(it != limiters_.end());
    if (it->second.expired()) {
      limiters_.erase(it);
    }
  }
}

Config::Config(
    const envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit& proto_config,
    Event::Dispatcher& dispatcher, Stats::Scope& scope, Runtime::Loader& runtime,
    Singleton::Manager& singleton_manager)
    : enabled_(proto_config.runtime_enabled(), runtime),
      stats_(generateStats(proto_config.stat_prefix(), scope)),
      shared_bucket_registry_(singleton_manager.getTyped<SharedRateLimitSingleton>(
          SINGLETON_MANAGER_REGISTERED_NAME(shared_local_ratelimit),
          []() { return std::make_shared<SharedRateLimitSingleton>(); })) {

  std::tie(rate_limiter_, shared_bucket_key_) = shared_bucket_registry_->get(proto_config, [&]() {
    return std::make_shared<Filters::Common::LocalRateLimit::LocalRateLimiterImpl>(
        std::chrono::milliseconds(
            PROTOBUF_GET_MS_REQUIRED(proto_config.token_bucket(), fill_interval)),
        proto_config.token_bucket().max_tokens(),
        PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config.token_bucket(), tokens_per_fill, 1),
        dispatcher,
        Protobuf::RepeatedPtrField<
            envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>());
  });
}

Config::~Config() {
  // Reset the rate_limiter_ first so that the weak_ptr will be expired() if this was the last
  // reference.
  rate_limiter_.reset();

  shared_bucket_registry_->removeIfUnused(shared_bucket_key_);
}

LocalRateLimitStats Config::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = "local_rate_limit." + prefix;
  return {ALL_LOCAL_RATE_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

bool Config::canCreateConnection() { return rate_limiter_->requestAllowed({}).allowed; }

Network::FilterStatus Filter::onNewConnection() {
  if (!config_->enabled()) {
    ENVOY_CONN_LOG(trace, "local_rate_limit: runtime disabled", read_callbacks_->connection());
    return Network::FilterStatus::Continue;
  }

  if (!config_->canCreateConnection()) {
    config_->stats().rate_limited_.inc();
    ENVOY_CONN_LOG(trace, "local_rate_limit: rate limiting connection",
                   read_callbacks_->connection());
    read_callbacks_->connection().streamInfo().setResponseFlag(
        StreamInfo::CoreResponseFlag::UpstreamRetryLimitExceeded);
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush,
                                        "local_ratelimit_close_over_limit");
    return Network::FilterStatus::StopIteration;
  }

  return Network::FilterStatus::Continue;
}

} // namespace LocalRateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
