#include "extensions/filters/http/cache/config.h"
#include "extensions/filters/http/cache/simple_http_cache.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

class CacheFilterFactoryTest:public ::testing::Test{
 protected:
  envoy::config::filter::http::cache::v2alpha::Cache config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  CacheFilterFactory factory_;
  Http::MockFilterChainFactoryCallbacks filter_callback_;
};

TEST_F(CacheFilterFactoryTest, Basic) {
  config_.set_name("SimpleHttpCache");
  Http::FilterFactoryCb cb = factory_.createFilterFactoryFromProto(config_, "stats", context_);
  Http::StreamFilterSharedPtr filter;
  EXPECT_CALL(filter_callback_, addStreamFilter(_)).WillOnce(::testing::SaveArg<0>(&filter));
  cb(filter_callback_);
  ASSERT(filter);
  ASSERT(dynamic_cast<CacheFilter*>(filter.get()));
}

TEST_F(CacheFilterFactoryTest, NoName) {
  Http::FilterFactoryCb cb = factory_.createFilterFactoryFromProto(config_, "stats", context_);
  EXPECT_THROW(cb(filter_callback_), ProtoValidationException);;
}

TEST_F(CacheFilterFactoryTest, UnregisteredName) {
  config_.set_name("Wrong");
  Http::FilterFactoryCb cb = factory_.createFilterFactoryFromProto(config_, "stats", context_);
  EXPECT_THROW(cb(filter_callback_), ProtoValidationException);;
}
}  // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
