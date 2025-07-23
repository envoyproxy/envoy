#include "source/extensions/bootstrap/reverse_tunnel/reverse_tunnel_acceptor.h"
#include "source/extensions/bootstrap/reverse_tunnel/reverse_tunnel_initiator.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {
namespace {

/**
 * Test ReverseTunnelAcceptor factory creation and basic functionality.
 */
TEST(ReverseTunnelTest, AcceptorFactoryCreation) {
  ReverseTunnelAcceptorFactory factory;
  EXPECT_EQ(factory.name(),
            "envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface");

  // Test factory creation through public interface
  auto empty_config = factory.createEmptyConfigProto();
  EXPECT_NE(empty_config, nullptr);
}

/**
 * Test ReverseTunnelInitiator factory creation and basic functionality.
 */
TEST(ReverseTunnelTest, InitiatorFactoryCreation) {
  ReverseTunnelInitiatorFactory factory;
  EXPECT_EQ(factory.name(),
            "envoy.bootstrap.reverse_connection.downstream_reverse_connection_socket_interface");

  // Test factory creation through public interface
  auto empty_config = factory.createEmptyConfigProto();
  EXPECT_NE(empty_config, nullptr);
}

/**
 * Test basic configuration validation.
 */
TEST(ReverseTunnelTest, ConfigurationValidation) {
  // Test acceptor configuration
  envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
      UpstreamReverseConnectionSocketInterface acceptor_config;
  const std::string acceptor_yaml = R"EOF(
stat_prefix: "reverse_connection_test"
)EOF";
  TestUtility::loadFromYaml(acceptor_yaml, acceptor_config);
  EXPECT_EQ(acceptor_config.stat_prefix(), "reverse_connection_test");

  // Test initiator configuration
  envoy::extensions::bootstrap::reverse_connection_socket_interface::v3::
      DownstreamReverseConnectionSocketInterface initiator_config;
  const std::string initiator_yaml = R"EOF(
stat_prefix: "reverse_connection_test"
)EOF";
  TestUtility::loadFromYaml(initiator_yaml, initiator_config);
  EXPECT_EQ(initiator_config.stat_prefix(), "reverse_connection_test");
}

/**
 * Test factory pattern implementation.
 */
TEST(ReverseTunnelTest, FactoryPatternImplementation) {
  // Test acceptor factory
  ReverseTunnelAcceptorFactory acceptor_factory;
  EXPECT_EQ(acceptor_factory.name(),
            "envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface");

  // Test initiator factory
  ReverseTunnelInitiatorFactory initiator_factory;
  EXPECT_EQ(initiator_factory.name(),
            "envoy.bootstrap.reverse_connection.downstream_reverse_connection_socket_interface");

  // Test empty config creation
  auto acceptor_config = acceptor_factory.createEmptyConfigProto();
  auto initiator_config = initiator_factory.createEmptyConfigProto();

  EXPECT_NE(acceptor_config, nullptr);
  EXPECT_NE(initiator_config, nullptr);
}

} // namespace
} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
