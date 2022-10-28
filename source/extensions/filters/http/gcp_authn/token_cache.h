#pragma once
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

#include "jwt_verify_lib/jwt.h"
#include "jwt_verify_lib/verify.h"
#include "simple_lru_cache/simple_lru_cache_inl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

template <typename TokenType>
using LRUCache = ::google::simple_lru_cache::SimpleLRUCache<std::string, TokenType>;
using JwtToken = ::google::jwt_verify::Jwt;

template <typename TokenType> class TokenCacheImpl : public Logger::Loggable<Logger::Id::init> {
public:
  TokenCacheImpl(const envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig& config,
                 TimeSource& time_source)
      : lru_cache_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, cache_size, 0)),
        time_source_(time_source) {}

  TokenCacheImpl() = delete;
  TokenType* lookUp(const std::string& key);
  void insert(const std::string& key, std::unique_ptr<TokenType>&& token);
  TokenType* validateTokenAndReturn(const std::string& key, TokenType* const found_token);

  ~TokenCacheImpl() {
    // Remove all entries from the cache.
    lru_cache_.clear();
  }

private:
  LRUCache<TokenType> lru_cache_;
  TimeSource& time_source_;
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

struct TokenCache {
  TokenCache(const envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig& cache_config,
             Envoy::Server::Configuration::FactoryContext& context)
      : tls(context.threadLocal()) {
    tls.set([cache_config](Envoy::Event::Dispatcher& dispatcher) {
      return std::make_shared<ThreadLocalCache>(cache_config, dispatcher.timeSource());
    });
  }
  Envoy::ThreadLocal::TypedSlot<ThreadLocalCache> tls;
};

template <typename TokenType> TokenType* TokenCacheImpl<TokenType>::lookUp(const std::string& key) {
  typename LRUCache<TokenType>::ScopedLookup lookup(&lru_cache_, key);
  if (lookup.found()) {
    TokenType* const found_token = lookup.value();
    return validateTokenAndReturn(key, found_token);
  }
  // Return `nullptr` if no entry is found.
  return nullptr;
}

template <typename TokenType>
void TokenCacheImpl<TokenType>::insert(const std::string& key, std::unique_ptr<TokenType>&& token) {
  // Release the token to transfer the ownership of token.
  lru_cache_.insert(key, token.release(), 1);
}

template <typename TokenType>
TokenType* TokenCacheImpl<TokenType>::validateTokenAndReturn(const std::string& key,
                                                             TokenType* const found_token) {
  if constexpr (std::is_same<TokenType, JwtToken>::value) {
    ASSERT(found_token != nullptr);
    // Verify the validness of the token by checking its expiration time field.
    if (found_token->verifyTimeConstraint(DateUtil::nowToSeconds(time_source_)) ==
        ::google::jwt_verify::Status::JwtExpired) {
      // Remove the expired entry.
      lru_cache_.remove(key);
    } else {
      // Return the valid token.
      return found_token;
    }
  }
  // Return `nullptr` if no valid token has been found.
  return nullptr;
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
