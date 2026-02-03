#include "source/extensions/filters/http/alternate_protocols_cache/config.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AlternateProtocolsCache {
namespace {

TEST(AlternateProtocolsCacheFilterConfigTest, AlternateProtocolsCacheFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  AlternateProtocolsCacheFilterFactory factory;
  envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig config;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context).value();
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, dispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(filter_callback, addStreamEncoderFilter(_));
  cb(filter_callback);
}

TEST(AlternateProtocolsCacheFilterConfigTest, AlternateProtocolsCacheFilterWithServerContext) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  AlternateProtocolsCacheFilterFactory factory;
  envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig config;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(config, "stats", context);
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, dispatcher()).WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(filter_callback, addStreamEncoderFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace AlternateProtocolsCache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
