#include "envoy/extensions/http/cache_v2/simple_http_cache/v3/config.pb.h"

#include "source/extensions/filters/http/cache_v2/cache_filter.h"
#include "source/extensions/filters/http/cache_v2/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace {

class CacheFilterFactoryTest : public ::testing::Test {
protected:
  envoy::extensions::filters::http::cache_v2::v3::CacheV2Config config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  CacheFilterFactory factory_;
  Http::MockFilterChainFactoryCallbacks filter_callback_;
};

TEST_F(CacheFilterFactoryTest, Basic) {
  config_.mutable_typed_config()->PackFrom(
      envoy::extensions::http::cache_v2::simple_http_cache::v3::SimpleHttpCacheV2Config());
  Http::FilterFactoryCb cb =
      factory_.createFilterFactoryFromProto(config_, "stats", context_).value();
  Http::StreamFilterSharedPtr filter;
  EXPECT_CALL(filter_callback_, addStreamFilter(_)).WillOnce(::testing::SaveArg<0>(&filter));
  cb(filter_callback_);
  ASSERT(filter);
  ASSERT(dynamic_cast<CacheFilter*>(filter.get()));
}

TEST_F(CacheFilterFactoryTest, Disabled) {
  config_.mutable_disabled()->set_value(true);
  Http::FilterFactoryCb cb =
      factory_.createFilterFactoryFromProto(config_, "stats", context_).value();
  Http::StreamFilterSharedPtr filter;
  EXPECT_CALL(filter_callback_, addStreamFilter(_)).WillOnce(::testing::SaveArg<0>(&filter));
  cb(filter_callback_);
  ASSERT(filter);
  ASSERT(dynamic_cast<CacheFilter*>(filter.get()));
}

TEST_F(CacheFilterFactoryTest, NoTypedConfig) {
  EXPECT_THROW(
      factory_.createFilterFactoryFromProto(config_, "stats", context_).status().IgnoreError(),
      EnvoyException);
}

TEST_F(CacheFilterFactoryTest, UnregisteredTypedConfig) {
  config_.mutable_typed_config()->PackFrom(
      envoy::extensions::filters::http::cache_v2::v3::CacheV2Config());
  EXPECT_THROW(
      factory_.createFilterFactoryFromProto(config_, "stats", context_).status().IgnoreError(),
      EnvoyException);
}

class FailToCreateCacheFactory : public HttpCacheFactory {
public:
  std::string name() const override {
    return std::string("envoy.extensions.http.cache_v2.fake_fail");
  }
  // Arbitrarily use "Key" as the proto type of the config because it's convenient,
  // and we have to register it as *some* type of proto message.
  ProtobufTypes::MessagePtr createEmptyConfigProto() override { return std::make_unique<Key>(); }
  absl::StatusOr<std::shared_ptr<CacheSessions>>
  getCache(const envoy::extensions::filters::http::cache_v2::v3::CacheV2Config&,
           Server::Configuration::FactoryContext&) override {
    return absl::InvalidArgumentError("intentional fail");
  }
};

static Registry::RegisterFactory<FailToCreateCacheFactory, HttpCacheFactory> register_;

TEST_F(CacheFilterFactoryTest, FactoryFailsToCreateCache) {
  config_.mutable_typed_config()->PackFrom(Key());
  EXPECT_THROW(
      factory_.createFilterFactoryFromProto(config_, "stats", context_).status().IgnoreError(),
      EnvoyException);
}

} // namespace
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
