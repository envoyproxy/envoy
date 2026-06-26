#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"

#include "source/extensions/filters/http/alternate_protocols_cache/config.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AlternateProtocolsCache {
namespace {

TEST(AlternateProtocolsCacheFilterConfigTest, AlternateProtocolsCacheFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  AlternateProtocolsCacheFilterFactory factory;
  envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig proto_config;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(filter_callback, dispatcher()).WillRepeatedly(testing::ReturnRef(dispatcher));
  EXPECT_CALL(filter_callback, addStreamEncoderFilter(_));
  cb(filter_callback);
}

TEST(AlternateProtocolsCacheFilterConfigTest, AlternateProtocolsCacheFilterWithServerContext) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  AlternateProtocolsCacheFilterFactory factory;
  envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig proto_config;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(filter_callback, dispatcher()).WillRepeatedly(testing::ReturnRef(dispatcher));
  EXPECT_CALL(filter_callback, addStreamEncoderFilter(_));
  cb(filter_callback);
}

// This test is only for coverage of the deprecated feature check.
TEST(AlternateProtocolsCacheFilterConfigTest,
     DEPRECATED_FEATURE_TEST(AlternateProtocolsCacheFilterLogging)) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  AlternateProtocolsCacheFilterFactory factory;
  envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig proto_config;
  proto_config.mutable_alternate_protocols_cache_options()->set_name("foo");
  EXPECT_LOG_CONTAINS("warn",
                      "Using deprecated and ignored alternate_protocols_cache_options in "
                      "alternate_protocols_cache config.",
                      (void)factory.createFilterFactoryFromProto(proto_config, "stats", context));
}

} // namespace
} // namespace AlternateProtocolsCache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
