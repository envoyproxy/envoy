#include "extensions/filters/http/grpc_http1_bridge/config.h"

#include "test/mocks/server/mocks.h"

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
  const auto proto_config = factory.createEmptyConfigProto().get();
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

} // namespace
} // namespace GrpcHttp1Bridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
