#include "extensions/filters/http/jwt_authn/jwt_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

// The default number of entries in JWT cache is 100.
constexpr int kJwtCacheSize = 100;

class JwtCacheImpl : public JwtCache {
public:
  JwtCacheImpl(bool enable_cache, int cache_size) {
    if (enable_cache) {
      if (cache_size == 0) {
        jwt_cache_ =
            std::make_unique<SimpleLRUCache<std::string, ::google::jwt_verify::Jwt>>(kJwtCacheSize);
      } else {
        jwt_cache_ =
            std::make_unique<SimpleLRUCache<std::string, ::google::jwt_verify::Jwt>>(cache_size);
      }
    }
  }

  ~JwtCacheImpl() { jwt_cache_->clear(); }

  ::google::jwt_verify::Jwt* find(const std::string& token) override {
    if (!jwt_cache_) {
      return nullptr;
    }
    ::google::jwt_verify::Jwt* found_jwt{};
    SimpleLRUCache<std::string, ::google::jwt_verify::Jwt>::ScopedLookup lookup(jwt_cache_.get(),
                                                                                token);
    if (lookup.found()) {
      found_jwt = lookup.value();
      if (found_jwt) {
        if (found_jwt->verifyTimeConstraint(absl::ToUnixSeconds(absl::Now())) ==
            ::google::jwt_verify::Status::JwtExpired) {
          jwt_cache_->remove(token);
        }
      }
    }
    return found_jwt;
  }

  void add(const std::string& token, std::unique_ptr<::google::jwt_verify::Jwt>&& jwt) override {
    if (jwt_cache_) {
      // pass the ownership of jwt to cache
      jwt_cache_->insert(token, jwt.release(), 1);
    }
  }

private:
  std::unique_ptr<SimpleLRUCache<std::string, ::google::jwt_verify::Jwt>> jwt_cache_;
};
} // namespace

JwtCachePtr JwtCache::create(bool enable_cache, int cache_size) {
  return std::make_unique<JwtCacheImpl>(enable_cache, cache_size);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
