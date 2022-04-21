#include "source/extensions/filters/network/meta_protocol_proxy/filters/router/config.h"

#include "test/extensions/filters/network/meta_protocol_proxy/mocks/filter.h"
#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {
namespace Router {
namespace {

TEST(RouterFactoryTest, RouterFactoryTest) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  RouterFactory factory;

  ProtobufWkt::Struct proto_config;

  EXPECT_NO_THROW(factory.createFilterFactoryFromProto(proto_config, "test", factory_context));

  EXPECT_NE(nullptr, factory.createEmptyConfigProto());
  EXPECT_EQ(nullptr, factory.createEmptyRouteConfigProto());
  EXPECT_EQ(nullptr, factory.createRouteSpecificFilterConfig(
                         proto_config, factory_context.getServerFactoryContext(),
                         factory_context.messageValidationVisitor()));
  EXPECT_EQ("envoy.filters.meta_protocol.router", factory.name());
  EXPECT_EQ(true, factory.isTerminalFilter());

  auto fn = factory.createFilterFactoryFromProto(proto_config, "test", factory_context);

  NiceMock<MockFilterChainFactoryCallbacks> mock_cb;

  EXPECT_CALL(mock_cb, addDecoderFilter(_)).Times(1);
  fn(mock_cb);
}

} // namespace
} // namespace Router
} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
