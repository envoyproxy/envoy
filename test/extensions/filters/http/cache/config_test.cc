#include "envoy/extensions/http/cache/simple_http_cache/v3/config.pb.h"

#include "source/extensions/filters/http/cache/cache_filter.h"
#include "source/extensions/filters/http/cache/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

class CacheFilterFactoryTest : public ::testing::Test {
protected:
  envoy::extensions::filters::http::cache::v3::CacheConfig config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  CacheFilterFactory factory_;
  Http::MockFilterChainFactoryCallbacks filter_callback_;
};

TEST_F(CacheFilterFactoryTest, Basic) {
  config_.mutable_typed_config()->PackFrom(
      envoy::extensions::http::cache::simple_http_cache::v3::SimpleHttpCacheConfig());
  Http::FilterFactoryCb cb = factory_.createFilterFactoryFromProto(config_, "stats", context_);
  Http::StreamFilterSharedPtr filter;
  EXPECT_CALL(filter_callback_, addStreamFilter(_)).WillOnce(::testing::SaveArg<0>(&filter));
  cb(filter_callback_);
  ASSERT(filter);
  ASSERT(dynamic_cast<CacheFilter*>(filter.get()));
}

TEST_F(CacheFilterFactoryTest, NoTypedConfig) {
  EXPECT_THROW(factory_.createFilterFactoryFromProto(config_, "stats", context_), EnvoyException);
}

TEST_F(CacheFilterFactoryTest, UnregisteredTypedConfig) {
  config_.mutable_typed_config()->PackFrom(
      envoy::extensions::filters::http::cache::v3::CacheConfig());
  EXPECT_THROW(factory_.createFilterFactoryFromProto(config_, "stats", context_), EnvoyException);
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
