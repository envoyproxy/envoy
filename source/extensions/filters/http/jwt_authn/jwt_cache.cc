#include "source/extensions/filters/http/jwt_authn/jwt_cache.h"

#include "source/common/common/assert.h"

#include "simple_lru_cache/simple_lru_cache_inl.h"

using ::google::simple_lru_cache::SimpleLRUCache;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

// The default number of entries in JWT cache is 100.
constexpr int kJwtCacheDefaultSize = 100;
// The maximum size of JWT to be cached.
constexpr int kMaxJwtSizeForCache = 4 * 1024; // 4KiB

class JwtCacheImpl : public JwtCache {
public:
  JwtCacheImpl(bool enable_cache, const JwtCacheConfig& config, TimeSource& time_source)
      : time_source_(time_source) {
    if (enable_cache) {
      // if cache_size is 0, it is not specified in the config, use default
      auto cache_size =
          config.jwt_cache_size() == 0 ? kJwtCacheDefaultSize : config.jwt_cache_size();
      jwt_lru_cache_ =
          std::make_unique<SimpleLRUCache<std::string, ::google::jwt_verify::Jwt>>(cache_size);
    }
  }

  ~JwtCacheImpl() override {
    if (jwt_lru_cache_) {
      jwt_lru_cache_->clear();
    }
  }

  ::google::jwt_verify::Jwt* lookup(const std::string& token) override {
    if (!jwt_lru_cache_) {
      return nullptr;
    }
    SimpleLRUCache<std::string, ::google::jwt_verify::Jwt>::ScopedLookup lookup(
        jwt_lru_cache_.get(), token);
    if (lookup.found()) {
      ::google::jwt_verify::Jwt* const found_jwt = lookup.value();
      ASSERT(found_jwt != nullptr);
      if (found_jwt->verifyTimeConstraint(DateUtil::nowToSeconds(time_source_)) !=
          ::google::jwt_verify::Status::JwtExpired) {
        return found_jwt;
      } else {
        jwt_lru_cache_->remove(token);
      }
    }
    return nullptr;
  }

  void insert(const std::string& token, std::unique_ptr<::google::jwt_verify::Jwt>&& jwt) override {
    if (jwt_lru_cache_ && token.size() <= kMaxJwtSizeForCache) {
      // pass the ownership of jwt to cache
      jwt_lru_cache_->insert(token, jwt.release(), 1);
    }
  }

private:
  std::unique_ptr<SimpleLRUCache<std::string, ::google::jwt_verify::Jwt>> jwt_lru_cache_;
  TimeSource& time_source_;
};
} // namespace

JwtCachePtr JwtCache::create(bool enable_cache, const JwtCacheConfig& config,
                             TimeSource& time_source) {
  return std::make_unique<JwtCacheImpl>(enable_cache, config, time_source);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
