#include "source/extensions/filters/http/cors/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cors {
namespace {

TEST(CorsFilterConfigTest, CorsFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  CorsFilterFactory factory;
  envoy::extensions::filters::http::cors::v3::Cors config;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context).value();
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(CorsFilterConfigTest, CorsFilterWithServerContext) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  CorsFilterFactory factory;
  envoy::extensions::filters::http::cors::v3::Cors config;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(config, "stats", context);
  NiceMock<Http::MockFilterChainFactoryCallbacks> filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace Cors
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
