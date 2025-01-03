#include "source/extensions/filters/http/rate_limit_quota/config.h"

#include <chrono>
#include <memory>
#include <utility>

#include "envoy/event/dispatcher.h"
#include "envoy/grpc/async_client_manager.h"
#include "envoy/http/filter.h"
#include "envoy/http/filter_factory.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/filter_config.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/type/v3/ratelimit_strategy.pb.h"

#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/filter.h"
#include "source/extensions/filters/http/rate_limit_quota/global_client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

// Object to hold TLS slots after the factory itself has been cleaned up.
struct TlsStore {
  TlsStore(Server::Configuration::FactoryContext& context)
      : global_client_tls(context.serverFactoryContext().threadLocal()),
        buckets_tls(context.serverFactoryContext().threadLocal()) {}

  ThreadLocal::TypedSlot<ThreadLocalGlobalRateLimitClientImpl> global_client_tls;
  ThreadLocal::TypedSlot<ThreadLocalBucketsCache> buckets_tls;
};

Http::FilterFactoryCb RateLimitQuotaFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig&
        filter_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  // Filter config const object is created on the main thread and shared between
  // worker threads.
  FilterConfigConstSharedPtr config = std::make_shared<
      envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig>(
      filter_config);

  Grpc::GrpcServiceConfigWithHashKey config_with_hash_key =
      Grpc::GrpcServiceConfigWithHashKey(config->rlqs_server());

  // Quota bucket & global client TLS objects are created with the config and
  // kept alive via shared_ptr to a storage struct. The local rate limit client
  // in each filter instance assumes that the slot will outlive them.
  std::shared_ptr<TlsStore> tls_store = std::make_shared<TlsStore>(context);
  auto tl_buckets_cache =
      std::make_shared<ThreadLocalBucketsCache>(std::make_shared<BucketsCache>());
  tls_store->buckets_tls.set(
      [tl_buckets_cache]([[maybe_unused]] Envoy::Event::Dispatcher& dispatcher) {
        return tl_buckets_cache;
      });

  // TODO(bsurber): Implement report timing & usage aggregation based on each
  // bucket's reporting_interval field. Currently this is not supported and all
  // usage is reported on a hardcoded interval.
  std::chrono::milliseconds reporting_interval(5000);

  // Create the global client resource to be shared via TLS to all worker
  // threads (accessed through a filter-specific LocalRateLimitClient).
  auto tl_global_client = std::make_shared<ThreadLocalGlobalRateLimitClientImpl>(
      createGlobalRateLimitClientImpl(context, filter_config.domain(), reporting_interval,
                                      tls_store->buckets_tls, config_with_hash_key));
  tls_store->global_client_tls.set(
      [tl_global_client]([[maybe_unused]] Envoy::Event::Dispatcher& dispatcher) {
        return tl_global_client;
      });

  RateLimitOnMatchActionContext action_context;
  RateLimitQuotaValidationVisitor visitor;
  Matcher::MatchTreeFactory<Http::HttpMatchingData, RateLimitOnMatchActionContext> matcher_factory(
      action_context, context.serverFactoryContext(), visitor);

  Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> matcher = nullptr;
  if (config->has_bucket_matchers()) {
    matcher = matcher_factory.create(config->bucket_matchers())();
  }

  return [&, config = std::move(config), config_with_hash_key, tls_store = std::move(tls_store),
          matcher = std::move(matcher)](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    std::unique_ptr<RateLimitClient> local_client =
        createLocalRateLimitClient(tls_store->global_client_tls, tls_store->buckets_tls);

    callbacks.addStreamFilter(std::make_shared<RateLimitQuotaFilter>(
        config, context, std::move(local_client), config_with_hash_key, matcher));
  };
}

/**
 * Static registration for the filter. @see RegisterFactory.
 */
REGISTER_FACTORY(RateLimitQuotaFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
