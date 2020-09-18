#include "envoy/extensions/filters/network/dubbo_proxy/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/router/v3/router.pb.validate.h"

#include "extensions/filters/network/dubbo_proxy/filters/well_known_names.h"
#include "extensions/filters/network/dubbo_proxy/router/config.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

TEST(DubboProxyRouterFilterConfigTest, RouterV2Alpha1Filter) {
  envoy::extensions::filters::network::dubbo_proxy::router::v3::Router router_config;
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  DubboFilters::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(router_config, "stats", context);
  DubboFilters::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addDecoderFilter(_));
  cb(filter_callback);
}

TEST(DubboProxyRouterFilterConfigTest, RouterFilterWithEmptyProtoConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RouterFilterConfig factory;
  DubboFilters::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*factory.createEmptyConfigProto(), "stats", context);
  DubboFilters::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addDecoderFilter(_));
  cb(filter_callback);
}

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
