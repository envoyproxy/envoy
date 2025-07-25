#include "source/extensions/filters/network/generic_proxy/router/config.h"

#include "test/extensions/filters/network/generic_proxy/mocks/filter.h"
#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Router {
namespace {

TEST(RouterFactoryTest, RouterFactoryTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  RouterFactory factory;

  envoy::extensions::filters::network::generic_proxy::router::v3::Router proto_config;

  EXPECT_NO_THROW(factory.createFilterFactoryFromProto(proto_config, "test", factory_context));

  EXPECT_NE(nullptr, factory.createEmptyConfigProto());
  EXPECT_EQ(nullptr, factory.createEmptyRouteConfigProto());
  EXPECT_EQ(nullptr, factory.createRouteSpecificFilterConfig(
                         proto_config, factory_context.serverFactoryContext(),
                         factory_context.messageValidationVisitor()));
  EXPECT_EQ("envoy.filters.generic.router", factory.name());
  EXPECT_EQ(true, factory.isTerminalFilter());

  proto_config.set_bind_upstream_connection(true);
  auto fn = factory.createFilterFactoryFromProto(proto_config, "test", factory_context);

  NiceMock<MockFilterChainFactoryCallbacks> mock_cb;

  EXPECT_CALL(mock_cb, addDecoderFilter(_));
  fn(mock_cb);
}

} // namespace
} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
