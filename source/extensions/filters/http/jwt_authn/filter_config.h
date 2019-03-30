#pragma once

#include "envoy/api/api.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"

#include "common/config/metadata.h"

#include "extensions/filters/http/jwt_authn/matcher.h"
#include "extensions/filters/http/jwt_authn/verifier.h"

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
  ThreadLocalCache(
      const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& config,
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

// clang-format off
#define ALL_JWT_AUTHN_FILTER_STATS(COUNTER)                                                        \
  COUNTER(allowed)                                                                                 \
  COUNTER(denied)
// clang-format on

/**
 * Wrapper struct for jwt_authn filter stats. @see stats_macros.h
 */
struct JwtAuthnFilterStats {
  ALL_JWT_AUTHN_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * The filter config object to hold config and relevant objects.
 */
class FilterConfig : public Logger::Loggable<Logger::Id::config>, public AuthFactory {
public:
  virtual ~FilterConfig() {}

  FilterConfig(
      const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context)
      : proto_config_(proto_config), stats_(generateStats(stats_prefix, context.scope())),
        tls_(context.threadLocal().allocateSlot()), cm_(context.clusterManager()),
        time_source_(context.dispatcher().timeSource()), api_(context.api()) {
    ENVOY_LOG(info, "Loaded JwtAuthConfig: {}", proto_config_.DebugString());
    tls_->set([this](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
      return std::make_shared<ThreadLocalCache>(proto_config_, time_source_, api_);
    });
    extractor_ = Extractor::create(proto_config_);

    for (const auto& rule : proto_config_.rules()) {
      rule_pairs_.emplace_back(
          Matcher::create(rule),
          Verifier::create(rule.requires(), proto_config_.providers(), *this, getExtractor()));
    }

    if (proto_config_.has_metadata_rules()) {
      metadata_filter_ = proto_config_.metadata_rules().filter();
      for (const auto& path : proto_config_.metadata_rules().path()) {
        metadata_path_.push_back(path);
      }
      for (const auto& it : proto_config_.metadata_rules().requires()) {
        metadata_verifiers_.emplace(it.first, Verifier::create(it.second, proto_config_.providers(),
                                                               *this, getExtractor()));
      }
    }
  }

  JwtAuthnFilterStats& stats() { return stats_; }

  // Get the Config.
  const ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication&
  getProtoConfig() const {
    return proto_config_;
  }

  // Get per-thread cache object.
  ThreadLocalCache& getCache() const { return tls_->getTyped<ThreadLocalCache>(); }

  Upstream::ClusterManager& cm() const { return cm_; }
  TimeSource& timeSource() const { return time_source_; }

  // Get the token  extractor.
  const Extractor& getExtractor() const { return *extractor_; }

  // Finds the matcher that matched the header
  virtual const Verifier* findVerifier(const Http::HeaderMap& headers,
                                       const envoy::api::v2::core::Metadata& metadata) const {
    for (const auto& pair : rule_pairs_) {
      if (pair.matcher_->matches(headers)) {
        return pair.verifier_.get();
      }
    }
    if (metadata_path_.size() > 0 && metadata_verifiers_.size() > 0) {
      const auto& value =
          Envoy::Config::Metadata::metadataValue(metadata, metadata_filter_, metadata_path_);
      if (value.kind_case() == ProtobufWkt::Value::kStringValue) {
        ENVOY_LOG(debug, "use metadata_rules key: {}", value.string_value());
        const auto& it = metadata_verifiers_.find(value.string_value());
        if (it != metadata_verifiers_.end()) {
          return it->second.get();
        }
      }
    }
    return nullptr;
  }

  // methods for AuthFactory interface. Factory method to help create authenticators.
  AuthenticatorPtr create(const ::google::jwt_verify::CheckAudience* check_audience,
                          const absl::optional<std::string>& provider,
                          bool allow_failed) const override {
    return Authenticator::create(check_audience, provider, allow_failed, getCache().getJwksCache(),
                                 cm(), Common::JwksFetcher::create, timeSource());
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
  ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication proto_config_;
  // The stats for the filter.
  JwtAuthnFilterStats stats_;
  // Thread local slot to store per-thread auth store
  ThreadLocal::SlotPtr tls_;
  // the cluster manager object.
  Upstream::ClusterManager& cm_;
  // The object to extract tokens.
  ExtractorConstPtr extractor_;
  // The list of rule matchers.
  std::vector<MatcherVerifierPair> rule_pairs_;
  // The metadata filter name.
  std::string metadata_filter_;
  // The metadata path.
  std::vector<std::string> metadata_path_;
  // The metadata verifier map.
  std::unordered_map<std::string, VerifierConstPtr> metadata_verifiers_;
  TimeSource& time_source_;
  Api::Api& api_;
};
typedef std::shared_ptr<FilterConfig> FilterConfigSharedPtr;

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
