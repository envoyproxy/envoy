#include "source/extensions/filters/listener/local_ratelimit/local_ratelimit.h"

#include "source/common/config/well_known_names.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace LocalRateLimit {

FilterConfig::FilterConfig(
    const envoy::extensions::filters::listener::local_ratelimit::v3::LocalRateLimit& proto_config,
    Event::Dispatcher& dispatcher, Stats::Scope& scope, Runtime::Loader& runtime)
    : enabled_(proto_config.runtime_enabled(), runtime),
      stats_(generateStats(proto_config.stat_prefix(), scope)),
      rate_limiter_(
          std::chrono::milliseconds(
              PROTOBUF_GET_MS_REQUIRED(proto_config.token_bucket(), fill_interval)),
          proto_config.token_bucket().max_tokens(),
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config.token_bucket(), tokens_per_fill, 1),
          dispatcher,
          Protobuf::RepeatedPtrField<
              envoy::extensions::common::ratelimit::v3::LocalRateLimitDescriptor>()) {}

bool FilterConfig::canCreateConnection() { return rate_limiter_.requestAllowed({}).allowed; }

LocalRateLimitStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  // listener_local_ratelimit.(<stat_prefix>.)
  Stats::TaggedStatName stat_prefix(
      scope.symbolTable(), "listener_local_ratelimit",
      {{Envoy::Config::TagNames::get().LOCAL_LISTENER_RATELIMIT_PREFIX, prefix}},
      absl::StrCat("listener_local_ratelimit.", prefix, "."));
  return {ALL_LOCAL_RATE_LIMIT_STATS(POOL_COUNTER_TAGGED(scope, stat_prefix))};
}

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  if (!config_->enabled()) {
    ENVOY_LOG(trace, "local_rate_limit: runtime disabled. remote address: {}",
              cb.socket().connectionInfoProvider().remoteAddress()->asString());
    return Network::FilterStatus::Continue;
  }

  if (!config_->canCreateConnection()) {
    ENVOY_LOG(debug, "local_rate_limit: rate limiting socket. remote address: {}",
              cb.socket().connectionInfoProvider().remoteAddress()->asString());

    config_->stats().rate_limited_.inc();
    cb.socket().ioHandle().close();
    return Network::FilterStatus::StopIteration;
  }

  return Network::FilterStatus::Continue;
}

} // namespace LocalRateLimit
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
