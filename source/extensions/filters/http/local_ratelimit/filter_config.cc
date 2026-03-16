#include "source/extensions/filters/http/local_ratelimit/filter_config.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"
#include "envoy/extensions/filters/http/local_ratelimit/v3/local_rate_limit.pb.h"

#include "source/common/router/config_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace LocalRateLimitFilter {

const std::string& PerConnectionRateLimiter::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "per_connection_local_rate_limiter");
}

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::local_ratelimit::v3::LocalRateLimit& config,
    Server::Configuration::CommonFactoryContext& context, Stats::Scope& scope, const bool per_route)
    : dispatcher_(context.mainThreadDispatcher()), status_(toErrorCode(config.status().code())),
      stats_(generateStats(config.stat_prefix(), scope)),
      fill_interval_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(config.token_bucket(), fill_interval, 0))),
      max_tokens_(config.token_bucket().max_tokens()),
      tokens_per_fill_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.token_bucket(), tokens_per_fill, 1)),
      max_dynamic_descriptors_(
          config.has_max_dynamic_descriptors() ? config.max_dynamic_descriptors().value() : 20),
      descriptors_(config.descriptors()),
      rate_limit_per_connection_(config.local_rate_limit_per_downstream_connection()),
      always_consume_default_token_bucket_(
          config.has_always_consume_default_token_bucket()
              ? config.always_consume_default_token_bucket().value()
              : true),

      local_info_(context.localInfo()), runtime_(context.runtime()),
      filter_enabled_(
          config.has_filter_enabled()
              ? absl::optional<Envoy::Runtime::FractionalPercent>(
                    Envoy::Runtime::FractionalPercent(config.filter_enabled(), runtime_))
              : absl::nullopt),
      filter_enforced_(
          config.has_filter_enforced()
              ? absl::optional<Envoy::Runtime::FractionalPercent>(
                    Envoy::Runtime::FractionalPercent(config.filter_enforced(), runtime_))
              : absl::nullopt),
      response_headers_parser_(THROW_OR_RETURN_VALUE(
          Envoy::Router::HeaderParser::configure(config.response_headers_to_add()),
          Router::HeaderParserPtr)),
      request_headers_parser_(THROW_OR_RETURN_VALUE(
          Envoy::Router::HeaderParser::configure(config.request_headers_to_add_when_not_enforced()),
          Router::HeaderParserPtr)),
      stage_(static_cast<uint64_t>(config.stage())),
      has_descriptors_(!config.descriptors().empty()),
      enable_x_rate_limit_headers_(config.enable_x_ratelimit_headers() ==
                                   envoy::extensions::common::ratelimit::v3::DRAFT_VERSION_03),
      vh_rate_limits_(config.vh_rate_limits()),
      rate_limited_grpc_status_(
          config.rate_limited_as_resource_exhausted()
              ? absl::make_optional(Grpc::Status::WellKnownGrpcStatus::ResourceExhausted)
              : absl::nullopt) {
  // Note: no token bucket is fine for the global config, which would be the case for enabling
  //       the filter globally but disabled and then applying limits at the virtual host or
  //       route level. At the virtual or route level, it makes no sense to have an no token
  //       bucket so we throw an error. If there's no token bucket configured globally or
  //       at the vhost/route level, no rate limiting is applied.
  if (per_route && !config.has_token_bucket()) {
    throw EnvoyException("local rate limit token bucket must be set for per filter configs");
  }

  absl::Status creation_status;
  rate_limit_config_ = std::make_unique<Filters::Common::RateLimit::RateLimitConfig>(
      config.rate_limits(), context, creation_status);
  THROW_IF_NOT_OK_REF(creation_status);

  if (rate_limit_config_->empty()) {
    if (!config.descriptors().empty()) {
      ENVOY_LOG_FIRST_N(
          warn, 20,
          "'descriptors' is set but only used by route configuration. Please configure the local "
          "rate limit filter using the embedded 'rate_limits' field as route configuration for "
          "local rate limits will be ignored in the future.");
    }
  }

  Filters::Common::LocalRateLimit::ShareProviderSharedPtr share_provider;
  if (config.has_local_cluster_rate_limit()) {
    if (rate_limit_per_connection_) {
      throw EnvoyException("local_cluster_rate_limit is set and "
                           "local_rate_limit_per_downstream_connection is set to true");
    }
    if (!context.clusterManager().localClusterName().has_value()) {
      throw EnvoyException("local_cluster_rate_limit is set but no local cluster name is present");
    }

    // If the local cluster name is set then the relevant cluster must exist or the cluster
    // manager will fail to initialize.
    share_provider_manager_ = Filters::Common::LocalRateLimit::ShareProviderManager::singleton(
        dispatcher_, context.clusterManager(), context.singletonManager());
    if (!share_provider_manager_) {
      throw EnvoyException("local_cluster_rate_limit is set but no local cluster is present");
    }

    share_provider = share_provider_manager_->getShareProvider(config.local_cluster_rate_limit());
  }

  rate_limiter_ = std::make_unique<Filters::Common::LocalRateLimit::LocalRateLimiterImpl>(
      fill_interval_, max_tokens_, tokens_per_fill_, dispatcher_, descriptors_,
      always_consume_default_token_bucket_, std::move(share_provider), max_dynamic_descriptors_);
}

Filters::Common::LocalRateLimit::LocalRateLimiter::Result
FilterConfig::requestAllowed(absl::Span<const RateLimit::Descriptor> request_descriptors) const {
  return rate_limiter_->requestAllowed(request_descriptors);
}

LocalRateLimitStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = prefix + ".http_local_rate_limit";
  return {ALL_LOCAL_RATE_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

bool FilterConfig::enabled() const {
  return filter_enabled_.has_value() ? filter_enabled_->enabled() : false;
}

bool FilterConfig::enforced() const {
  return filter_enforced_.has_value() ? filter_enforced_->enabled() : false;
}

} // namespace LocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
