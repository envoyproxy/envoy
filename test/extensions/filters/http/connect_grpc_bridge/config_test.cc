#include "envoy/extensions/filters/http/connect_grpc_bridge/v3/config.pb.h"
#include "envoy/extensions/filters/http/connect_grpc_bridge/v3/config.pb.validate.h"

#include "source/extensions/filters/http/connect_grpc_bridge/config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectGrpcBridge {
namespace {

TEST(ConnectGrpcBridgeFilterConfigTest, ConnectGrpcBridgeFilter) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  ConnectGrpcFilterConfigFactory factory;
  envoy::extensions::filters::http::connect_grpc_bridge::v3::FilterConfig config;
  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_)).Times(AtLeast(1));
  cb(filter_callback);
}

} // namespace
} // namespace ConnectGrpcBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
