#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.validate.h"

#include "source/extensions/common/dynamic_forward_proxy/dns_cache_impl.h"
#include "source/extensions/filters/http/dynamic_forward_proxy/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace DynamicForwardProxy {
namespace {

TEST(DynamicForwardProxyFilterFactoryTest, RouteSpecificConfig) {
  DynamicForwardProxyFilterFactory factory;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  EXPECT_TRUE(proto_config.get());

  Router::RouteSpecificFilterConfigConstSharedPtr route_config =
      factory.createRouteSpecificFilterConfig(*proto_config, context,
                                              ProtobufMessage::getNullValidationVisitor());
  EXPECT_TRUE(route_config.get());
}

} // namespace
} // namespace DynamicForwardProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
