#include "source/extensions/filters/network/meta_protocol_proxy/filters/router/config.h"

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
}

} // namespace
} // namespace Router
} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
