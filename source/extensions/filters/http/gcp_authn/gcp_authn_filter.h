#pragma once
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"

#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_impl.h"

#include "jwt_verify_lib/jwt.h"
#include "jwt_verify_lib/verify.h"
#include "simple_lru_cache/simple_lru_cache_inl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

inline constexpr absl::string_view FilterName = "envoy.filters.http.gcp_authn";
inline const Envoy::Http::LowerCaseString& authorizationHeaderKey() {
  CONSTRUCT_ON_FIRST_USE(Envoy::Http::LowerCaseString, "Authorization");
}
/**
 * All stats for the gcp authentication filter. @see stats_macros.h
 */
#define ALL_GCP_AUTHN_FILTER_STATS(COUNTER) COUNTER(retrieve_audience_failed)

/**
 * Wrapper struct for stats. @see stats_macros.h
 */
struct GcpAuthnFilterStats {
  ALL_GCP_AUTHN_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

using FilterConfigProtoSharedPtr =
    std::shared_ptr<envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>;

template <typename TokenType>
using LRUCache = ::google::simple_lru_cache::SimpleLRUCache<std::string, TokenType>;
using JwtToken = ::google::jwt_verify::Jwt;

template <typename TokenType> class TokenCacheImpl : public Logger::Loggable<Logger::Id::init> {
public:
  TokenCacheImpl(const envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig& config,
                 TimeSource& time_source)
      : time_source_(time_source), size_(config.cache_size()) {
    lru_cache_ = std::make_unique<LRUCache<TokenType>>(size_);
  }

  TokenType* lookUp(std::string key);
  void insert(const std::string& key, std::unique_ptr<TokenType> token);

  LRUCache<TokenType>& lruCache() { return *lru_cache_; }
  int size() { return size_; }

  ~TokenCacheImpl() {
    if (lru_cache_) {
      // Remove all entries from the cache.
      lru_cache_->clear();
    }
  }

private:
  std::unique_ptr<LRUCache<TokenType>> lru_cache_;
  TimeSource& time_source_;
  int size_;
};

class ThreadLocalCache : public Envoy::ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalCache(const envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig& config,
                   TimeSource& time_source)
      : cache_(config, time_source) {}
  TokenCacheImpl<JwtToken>& cache() { return cache_; }

private:
  // The lifetime and ownership of cache object is tied to ThreadLocalCache object.
  TokenCacheImpl<JwtToken> cache_;
};

class TokenCache {
public:
  TokenCache(const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config,
             Envoy::Server::Configuration::FactoryContext& context)
      : tls(context.threadLocal()) {
    tls.set([config](Envoy::Event::Dispatcher& dispatcher) {
      return std::make_shared<ThreadLocalCache>(config.cache_config(), dispatcher.timeSource());
    });
  }
  Envoy::ThreadLocal::TypedSlot<ThreadLocalCache> tls;
};

class GcpAuthnFilter : public Http::PassThroughFilter,
                       public RequestCallbacks,
                       public Logger::Loggable<Logger::Id::filter> {
public:
  // State of this filter's communication with the external authentication service.
  // The filter has either not started calling the external service, in the middle of calling
  // it or has completed.
  enum class State { NotStarted, Calling, Complete };

  GcpAuthnFilter(
      const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config,
      Server::Configuration::FactoryContext& context, const std::string& stats_prefix,
      TokenCacheImpl<JwtToken>* token_cache = nullptr)
      : filter_config_(
            std::make_shared<envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>(
                config)),
        context_(context), client_(std::make_unique<GcpAuthnClient>(*filter_config_, context_)),
        stats_(generateStats(stats_prefix, context_.scope())), jwt_token_cache_(token_cache) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  void onDestroy() override;
  void onComplete(const Http::ResponseMessage* response_ptr) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  State state() { return state_; }
  GcpAuthnFilterStats& stats() { return stats_; }

  ~GcpAuthnFilter() override = default;

private:
  GcpAuthnFilterStats generateStats(const std::string& stats_prefix, Stats::Scope& scope) {
    // const std::string prefix = absl::StrCat(stats_prefix, "gcp_authn.");
    return {ALL_GCP_AUTHN_FILTER_STATS(POOL_COUNTER_PREFIX(scope, stats_prefix))};
  }
  FilterConfigProtoSharedPtr filter_config_;
  Server::Configuration::FactoryContext& context_;
  std::unique_ptr<GcpAuthnClient> client_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  // The pointer to request headers for header manipulation later.
  Envoy::Http::RequestHeaderMap* request_header_map_ = nullptr;

  GcpAuthnFilterStats stats_;

  bool initiating_call_{};
  State state_{State::NotStarted};
  std::string audience_str_;
  // This cache is optional.
  TokenCacheImpl<JwtToken>* jwt_token_cache_ = nullptr;
};

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
