#include "envoy/config/transport_socket/alts/v2alpha/alts.pb.validate.h"

#include "common/protobuf/protobuf.h"

#include "extensions/transport_sockets/alts/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Server::Configuration::MockTransportSocketFactoryContext;
using testing::_;
using testing::Invoke;
using testing::StrictMock;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Alts {

TEST(UpstreamAltsConfigTest, CreateSocketFactory) {
  MockTransportSocketFactoryContext factory_context;
  UpstreamAltsTransportSocketConfigFactory factory;

  ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();

  std::string yaml = R"EOF(
  handshaker_service: 169.254.169.254:8080
  peer_service_accounts: ["server-sa"]
  )EOF";
  MessageUtil::loadFromYaml(yaml, *config);

  auto socket_factory = factory.createTransportSocketFactory(*config, factory_context);

  EXPECT_NE(nullptr, socket_factory);
  EXPECT_TRUE(socket_factory->implementsSecureTransport());
}

TEST(DownstreamAltsConfigTest, CreateSocketFactory) {
  MockTransportSocketFactoryContext factory_context;
  DownstreamAltsTransportSocketConfigFactory factory;

  ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();

  std::string yaml = R"EOF(
  handshaker_service: 169.254.169.254:8080
  peer_service_accounts: ["server-sa"]
  )EOF";
  MessageUtil::loadFromYaml(yaml, *config);

  auto socket_factory = factory.createTransportSocketFactory(*config, factory_context, {});

  EXPECT_NE(nullptr, socket_factory);
  EXPECT_TRUE(socket_factory->implementsSecureTransport());
}

} // namespace Alts
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy