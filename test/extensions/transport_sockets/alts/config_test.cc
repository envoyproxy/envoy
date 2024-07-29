#include "source/common/protobuf/protobuf.h"
#include "source/common/singleton/manager_impl.h"
#include "source/extensions/transport_sockets/alts/config.h"

#include "test/mocks/server/transport_socket_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Server::Configuration::MockTransportSocketFactoryContext;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {
namespace {

TEST(UpstreamAltsConfigTest, CreateSocketFactory) {
  NiceMock<MockTransportSocketFactoryContext> factory_context;
  Singleton::ManagerImpl singleton_manager;
  EXPECT_CALL(factory_context.server_context_, singletonManager())
      .WillRepeatedly(ReturnRef(singleton_manager));
  UpstreamAltsTransportSocketConfigFactory factory;

  ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();

  std::string yaml = R"EOF(
  handshaker_service: 169.254.169.254:8080
  peer_service_accounts: ["server-sa"]
  )EOF";
  TestUtility::loadFromYaml(yaml, *config);

  auto socket_factory = factory.createTransportSocketFactory(*config, factory_context).value();

  EXPECT_NE(nullptr, socket_factory);
  EXPECT_TRUE(socket_factory->implementsSecureTransport());
}

TEST(DownstreamAltsConfigTest, CreateSocketFactory) {
  NiceMock<MockTransportSocketFactoryContext> factory_context;
  Singleton::ManagerImpl singleton_manager;
  EXPECT_CALL(factory_context.server_context_, singletonManager())
      .WillRepeatedly(ReturnRef(singleton_manager));
  DownstreamAltsTransportSocketConfigFactory factory;

  ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();

  std::string yaml = R"EOF(
  handshaker_service: 169.254.169.254:8080
  peer_service_accounts: ["server-sa"]
  )EOF";
  TestUtility::loadFromYaml(yaml, *config);

  auto socket_factory = factory.createTransportSocketFactory(*config, factory_context, {}).value();

  EXPECT_NE(nullptr, socket_factory);
  EXPECT_TRUE(socket_factory->implementsSecureTransport());
}

} // namespace
} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
