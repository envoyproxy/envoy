#pragma once

#include "envoy/api/api.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/router/string_accessor.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "extensions/filters/http/jwt_authn/matcher.h"
#include "extensions/filters/http/jwt_authn/verifier.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

/**
 * Making cache as a thread local object, its read/write operations don't need to be protected.
 * Now it only has jwks_cache, but in the future, it will have token cache: to cache the tokens
 * with their verification results.
 */
class ThreadLocalCache : public ThreadLocal::ThreadLocalObject {
public:
  // Load the config from envoy config.
  ThreadLocalCache(const envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication& config,
                   TimeSource& time_source, Api::Api& api) {
    jwks_cache_ = JwksCache::create(config, time_source, api);
  }

  // Get the JwksCache object.
  JwksCache& getJwksCache() { return *jwks_cache_; }

private:
  // The JwksCache object.
  JwksCachePtr jwks_cache_;
};

/**
 * All stats for the Jwt Authn filter. @see stats_macros.h
 */
#define ALL_JWT_AUTHN_FILTER_STATS(COUNTER)                                                        \
  COUNTER(allowed)                                                                                 \
  COUNTER(cors_preflight_bypassed)                                                                 \
  COUNTER(denied)

/**
 * Wrapper struct for jwt_authn filter stats. @see stats_macros.h
 */
struct JwtAuthnFilterStats {
  ALL_JWT_AUTHN_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * The filter config interface. It is an interface so that we can mock it in tests.
 */
class FilterConfig {
public:
  virtual ~FilterConfig() = default;

  virtual JwtAuthnFilterStats& stats() PURE;

  virtual bool bypassCorsPreflightRequest() const PURE;

  // Finds the matcher that matched the header
  virtual const Verifier* findVerifier(const Http::RequestHeaderMap& headers,
                                       const StreamInfo::FilterState& filter_state) const PURE;
};
using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * The filter config object to hold config and relevant objects.
 */
class FilterConfigImpl : public Logger::Loggable<Logger::Id::jwt>,
                         public FilterConfig,
                         public AuthFactory,
                         public std::enable_shared_from_this<FilterConfigImpl> {
public:
  ~FilterConfigImpl() override = default;

  // Finds the matcher that matched the header
  static std::shared_ptr<FilterConfigImpl>
  create(envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication proto_config,
         const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
    // We can't use make_shared here because the constructor of this class is private.
    std::shared_ptr<FilterConfigImpl> ptr(
        new FilterConfigImpl(proto_config, stats_prefix, context));
    ptr->init();
    return ptr;
  }

  // Get per-thread cache object.
  ThreadLocalCache& getCache() const { return tls_->getTyped<ThreadLocalCache>(); }

  Upstream::ClusterManager& cm() const { return cm_; }
  TimeSource& timeSource() const { return time_source_; }

  // FilterConfig

  JwtAuthnFilterStats& stats() override { return stats_; }

  bool bypassCorsPreflightRequest() const override { return proto_config_.bypass_cors_preflight(); }

  const Verifier* findVerifier(const Http::RequestHeaderMap& headers,
                               const StreamInfo::FilterState& filter_state) const override {
    for (const auto& pair : rule_pairs_) {
      if (pair.matcher_->matches(headers)) {
        return pair.verifier_.get();
      }
    }
    if (!filter_state_name_.empty() && !filter_state_verifiers_.empty() &&
        filter_state.hasData<Router::StringAccessor>(filter_state_name_)) {
      const auto& state = filter_state.getDataReadOnly<Router::StringAccessor>(filter_state_name_);
      ENVOY_LOG(debug, "use filter state value {} to find verifier.", state.asString());
      const auto& it = filter_state_verifiers_.find(state.asString());
      if (it != filter_state_verifiers_.end()) {
        return it->second.get();
      }
    }
    return nullptr;
  }

  // methods for AuthFactory interface. Factory method to help create authenticators.
  AuthenticatorPtr create(const ::google::jwt_verify::CheckAudience* check_audience,
                          const absl::optional<std::string>& provider, bool allow_failed,
                          bool allow_missing) const override {
    return Authenticator::create(check_audience, provider, allow_failed, allow_missing,
                                 getCache().getJwksCache(), cm(), Common::JwksFetcher::create,
                                 timeSource());
  }

private:
  FilterConfigImpl(envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication proto_config,
                   const std::string& stats_prefix, Server::Configuration::FactoryContext& context)
      : proto_config_(std::move(proto_config)),
        stats_(generateStats(stats_prefix, context.scope())),
        tls_(context.threadLocal().allocateSlot()), cm_(context.clusterManager()),
        time_source_(context.dispatcher().timeSource()), api_(context.api()) {}

  void init();

  JwtAuthnFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    const std::string final_prefix = prefix + "jwt_authn.";
    return {ALL_JWT_AUTHN_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
  }

  struct MatcherVerifierPair {
    MatcherVerifierPair(MatcherConstPtr matcher, VerifierConstPtr verifier)
        : matcher_(std::move(matcher)), verifier_(std::move(verifier)) {}
    MatcherConstPtr matcher_;
    VerifierConstPtr verifier_;
  };

  // The proto config.
  envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication proto_config_;
  // The stats for the filter.
  JwtAuthnFilterStats stats_;
  // Thread local slot to store per-thread auth store
  ThreadLocal::SlotPtr tls_;
  // the cluster manager object.
  Upstream::ClusterManager& cm_;
  // The list of rule matchers.
  std::vector<MatcherVerifierPair> rule_pairs_;
  // The filter state name to lookup filter_state_rules.
  std::string filter_state_name_;
  // The filter state verifier map from filter_state_rules.
  absl::flat_hash_map<std::string, VerifierConstPtr> filter_state_verifiers_;
  TimeSource& time_source_;
  Api::Api& api_;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
