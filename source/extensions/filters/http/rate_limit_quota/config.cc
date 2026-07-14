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
#include "source/extensions/filters/http/rate_limit_quota/filter_persistence.h"
#include "source/extensions/filters/http/rate_limit_quota/global_client_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/quota_bucket_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using TlsStore = GlobalTlsStores::TlsStore;

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

  RateLimitOnMatchActionContext action_context;
  RateLimitQuotaValidationVisitor visitor;
  visitor.setSupportKeepMatching(true);
  Matcher::MatchTreeFactory<Http::HttpMatchingData, RateLimitOnMatchActionContext> matcher_factory(
      action_context, context.serverFactoryContext(), visitor);

  Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> matcher = nullptr;
  if (config->has_bucket_matchers()) {
    matcher = matcher_factory.create(config->bucket_matchers())();
  }
  if (!visitor.errors().empty()) {
    throw EnvoyException(absl::StrJoin(visitor.errors(), "\n"));
  }

  std::string rlqs_server_target = config->rlqs_server().has_envoy_grpc()
                                       ? config->rlqs_server().envoy_grpc().cluster_name()
                                       : config->rlqs_server().google_grpc().target_uri();

  // Get the TLS store from the global map, or create one if it doesn't exist.
  std::shared_ptr<TlsStore> tls_store = GlobalTlsStores::getTlsStore(
      config_with_hash_key, context, rlqs_server_target, filter_config.domain());

  return [&, config = std::move(config), config_with_hash_key, tls_store = std::move(tls_store),
          matcher = std::move(matcher)](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    std::unique_ptr<RateLimitClient> local_client =
        createLocalRateLimitClient(tls_store->global_client.get(), tls_store->buckets_tls);

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
