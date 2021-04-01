#include <memory>

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/http/jwt_authn/jwt_cache.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtProvider;
using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

class JwtCacheTest : public testing::Test {
protected:
  const std::string config = R"({
      enable_jwt_cache: true,
      jwt_cache_size: 20
  })";
  void SetUp() override {
    jwt_ = std::make_unique<::google::jwt_verify::Jwt>();
    Status status = jwt_->parseFromString(GoodToken);
    if (status == Status::Ok) {
      setupCache(config);
    }
  }

  void setupCache(const std::string& config_str) {
    TestUtility::loadFromYaml(config_str, provider_);
    cache_ =
        JwtCache::create(provider_.enable_jwt_cache(), provider_.jwt_cache_size(), time_system_);
  }

  Event::SimulatedTimeSystem time_system_;
  JwtProvider provider_;
  JwtCachePtr cache_;
  std::unique_ptr<::google::jwt_verify::Jwt> jwt_;
};

// Checking Cache is enable.
TEST_F(JwtCacheTest, TestCacheEnable) { EXPECT_TRUE(provider_.enable_jwt_cache()); }

// Checking jwt_cache_size
TEST_F(JwtCacheTest, TestCacheSize) { EXPECT_EQ(provider_.jwt_cache_size(), 20); }

// Inserting good Jwt and looking in cache.
TEST_F(JwtCacheTest, TestInsertGoodJwt) {
  cache_->insert(GoodToken, std::move(jwt_));
  auto jwt_cache = cache_->lookup(GoodToken);
  EXPECT_TRUE(jwt_cache != nullptr);
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
