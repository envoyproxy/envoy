#pragma once

#include "envoy/api/api.h"
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
class FilterConfig : public Logger::Loggable<Logger::Id::jwt>, public AuthFactory {
public:
  ~FilterConfig() override = default;

  FilterConfig(::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication proto_config,
               const std::string& stats_prefix, Server::Configuration::FactoryContext& context)
      : proto_config_(std::move(proto_config)),
        stats_(generateStats(stats_prefix, context.scope())),
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

    if (proto_config_.has_filter_state_rules()) {
      filter_state_name_ = proto_config_.filter_state_rules().name();
      for (const auto& it : proto_config_.filter_state_rules().requires()) {
        filter_state_verifiers_.emplace(
            it.first,
            Verifier::create(it.second, proto_config_.providers(), *this, getExtractor()));
      }
    }
