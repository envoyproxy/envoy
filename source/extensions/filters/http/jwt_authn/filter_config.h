#pragma once

#include "envoy/api/api.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/router/string_accessor.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/extensions/filters/http/jwt_authn/matcher.h"
#include "source/extensions/filters/http/jwt_authn/stats.h"
#include "source/extensions/filters/http/jwt_authn/verifier.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

/**
 * The per-route filter config
 */
class PerRouteFilterConfig : public Envoy::Router::RouteSpecificFilterConfig {
public:
  PerRouteFilterConfig(
      const envoy::extensions::filters::http::jwt_authn::v3::PerRouteConfig& config)
      : config_(config) {}

  const envoy::extensions::filters::http::jwt_authn::v3::PerRouteConfig& config() const {
    return config_;
  }

private:
  const envoy::extensions::filters::http::jwt_authn::v3::PerRouteConfig config_;
};

/**
 * The filter config interface. It is an interface so that we can mock it in tests.
 */
class FilterConfig {
public:
  virtual ~FilterConfig() = default;

  virtual JwtAuthnFilterStats& stats() PURE;

  virtual bool bypassCorsPreflightRequest() const PURE;

  virtual bool stripFailureResponse() const PURE;

  // Finds the matcher that matched the header
  virtual const Verifier* findVerifier(const Http::RequestHeaderMap& headers,
                                       const StreamInfo::FilterState& filter_state) const PURE;

  // Finds the verifier based on per-route config. If fail, pair.second has the error message.
  virtual std::pair<const Verifier*, std::string>
  findPerRouteVerifier(const PerRouteFilterConfig& per_route) const PURE;
};
using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * The filter config object to hold config and relevant objects.
 */
class FilterConfigImpl : public Logger::Loggable<Logger::Id::jwt>,
                         public FilterConfig,
                         public AuthFactory {
public:
  FilterConfigImpl(envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication proto_config,
                   const std::string& stats_prefix, Server::Configuration::FactoryContext& context);

  ~FilterConfigImpl() override = default;

  // Get the JwksCache object.
  JwksCache& getJwksCache() const { return *jwks_cache_; }

  Upstream::ClusterManager& cm() const { return cm_; }
  TimeSource& timeSource() const { return time_source_; }

  // FilterConfig

  JwtAuthnFilterStats& stats() override { return stats_; }

  bool bypassCorsPreflightRequest() const override { return proto_config_.bypass_cors_preflight(); }

  bool stripFailureResponse() const override { return proto_config_.strip_failure_response(); }

  const Verifier* findVerifier(const Http::RequestHeaderMap& headers,
                               const StreamInfo::FilterState& filter_state) const override {
    for (const auto& [matcher, verifier] : rule_pairs_) {
      if (matcher->matches(headers)) {
        return verifier.get();
      }
    }
    if (!filter_state_name_.empty() && !filter_state_verifiers_.empty()) {
      if (auto state = filter_state.getDataReadOnly<Router::StringAccessor>(filter_state_name_);
          state != nullptr) {
        ENVOY_LOG(debug, "use filter state value {} to find verifier.", state->asString());
        const auto& it = filter_state_verifiers_.find(state->asString());
        if (it != filter_state_verifiers_.end()) {
          return it->second.get();
        }
      }
    }
    return nullptr;
  }

  std::pair<const Verifier*, std::string>
  findPerRouteVerifier(const PerRouteFilterConfig& per_route) const override;

  // methods for AuthFactory interface. Factory method to help create authenticators.
  AuthenticatorPtr create(const ::google::jwt_verify::CheckAudience* check_audience,
                          const absl::optional<std::string>& provider, bool allow_failed,
                          bool allow_missing) const override {
    return Authenticator::create(check_audience, provider, allow_failed, allow_missing,
                                 getJwksCache(), cm(), Common::JwksFetcher::create, timeSource());
  }

private:
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
  // JwksCache
  JwksCachePtr jwks_cache_;
  // the cluster manager object.
  Upstream::ClusterManager& cm_;
  // The list of rule matchers.
  std::vector<MatcherVerifierPair> rule_pairs_;
  // The filter state name to lookup filter_state_rules.
  std::string filter_state_name_;
  // The filter state verifier map from filter_state_rules.
  absl::flat_hash_map<std::string, VerifierConstPtr> filter_state_verifiers_;
  // The requirement_name to verifier map.
  absl::flat_hash_map<std::string, VerifierConstPtr> name_verifiers_;
  // all requirement_names for debug
  std::string all_requirement_names_;
  TimeSource& time_source_;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
