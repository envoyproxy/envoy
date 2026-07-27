#include "envoy/extensions/http/cache/simple_http_cache/v3/config.pb.h"

#include "source/extensions/filters/http/cache/cache_filter.h"
#include "source/extensions/filters/http/cache/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace {

using ::Envoy::StatusHelpers::HasStatusMessage;

class CacheFilterFactoryTest : public ::testing::Test {
protected:
  envoy::extensions::filters::http::cache::v3::CacheConfig config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  CacheFilterFactory factory_;
  Http::MockFilterChainFactoryCallbacks filter_callback_;
};

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

TEST_F(CacheFilterFactoryTest, DisabledWithServerFactoryContext) {
  config_.mutable_disabled()->set_value(true);
  Http::FilterFactoryCb cb =
      factory_.createHttpFilterFactoryFromProto(config_, "stats", context_.server_factory_context_)
          .value();
  Http::StreamFilterSharedPtr filter;
  EXPECT_CALL(filter_callback_, addStreamFilter(_)).WillOnce(::testing::SaveArg<0>(&filter));
  cb(filter_callback_);
  ASSERT(filter);
  ASSERT(dynamic_cast<CacheFilter*>(filter.get()));
}

TEST_F(CacheFilterFactoryTest, NoTypedConfig) {
  auto status_or = factory_.createFilterFactoryFromProto(config_, "stats", context_);
  EXPECT_THAT(status_or, HasStatusMessage("at least one of typed_config or disabled must be set"));
}

TEST_F(CacheFilterFactoryTest, UnregisteredTypedConfig) {
  std::ignore = config_.mutable_typed_config()->PackFrom(
      envoy::extensions::filters::http::cache::v3::CacheConfig());
  auto status_or = factory_.createFilterFactoryFromProto(config_, "stats", context_);
  EXPECT_THAT(status_or, HasStatusMessage(testing::HasSubstr(
                             "Didn't find a registered implementation for type")));
}

} // namespace
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
