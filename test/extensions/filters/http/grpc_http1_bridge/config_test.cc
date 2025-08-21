#include "source/extensions/filters/http/grpc_http1_bridge/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcHttp1Bridge {
namespace {

TEST(GrpcHttp1BridgeFilterConfigTest, GrpcHttp1BridgeFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  GrpcHttp1BridgeFilterConfig factory;
  envoy::extensions::filters::http::grpc_http1_bridge::v3::Config config;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace GrpcHttp1Bridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
