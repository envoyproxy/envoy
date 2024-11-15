#include <memory>

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/jwt_authn/jwt_cache.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

class JwtCacheTest : public testing::Test {
public:
  void setupCache(bool enable) {
    envoy::extensions::filters::http::jwt_authn::v3::JwtCacheConfig config;
    config.set_jwt_cache_size(0);
    cache_ = JwtCache::create(enable, config, time_system_);
  }

  void loadJwt(const char* jwt_str) {
    jwt_ = std::make_unique<::google::jwt_verify::Jwt>();
    Status status = jwt_->parseFromString(jwt_str);
    EXPECT_EQ(status, Status::Ok);
  }

  Event::SimulatedTimeSystem time_system_;
  JwtCachePtr cache_;
  std::unique_ptr<::google::jwt_verify::Jwt> jwt_;
};

TEST_F(JwtCacheTest, TestEnabledCache) {
  // setup an enabled cache
  setupCache(true);
  loadJwt(GoodToken);

  auto* origin_jwt = jwt_.get();
  cache_->insert(GoodToken, std::move(jwt_));
  // jwt ownership is moved into the cache.
  EXPECT_FALSE(jwt_);

  auto* jwt1 = cache_->lookup(GoodToken);
  EXPECT_TRUE(jwt1 != nullptr);
  EXPECT_EQ(jwt1, origin_jwt);

  auto* jwt2 = cache_->lookup(ExpiredToken);
  EXPECT_TRUE(jwt2 == nullptr);
}

TEST_F(JwtCacheTest, TestDisabledCache) {
  // setup a disabled cache
  setupCache(false);
  loadJwt(GoodToken);

  cache_->insert(GoodToken, std::move(jwt_));
  // jwt ownership is not moved into the cache.
  EXPECT_TRUE(jwt_);

  auto* jwt = cache_->lookup(GoodToken);
  // not found since cache is disabled.
  EXPECT_TRUE(jwt == nullptr);
}

TEST_F(JwtCacheTest, TestExpiredToken) {
  // setup an enabled cache
  setupCache(true);
  loadJwt(ExpiredToken);

  cache_->insert(ExpiredToken, std::move(jwt_));

  auto* jwt = cache_->lookup(ExpiredToken);
  // not be found since it is expired.
  EXPECT_TRUE(jwt == nullptr);
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
