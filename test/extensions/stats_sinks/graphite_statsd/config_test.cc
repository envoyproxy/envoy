#include "envoy/config/core/v3/address.pb.h"
#include "envoy/extensions/stat_sinks/graphite_statsd/v3/graphite_statsd.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/macros.h"
#include "source/common/config/well_known_names.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/stat_sinks/common/statsd/statsd.h"
#include "source/extensions/stat_sinks/graphite_statsd/config.h"

#include "test/mocks/server/instance.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace GraphiteStatsd {
namespace {

const std::string& getStatSinkName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.stat_sinks.graphite_statsd");
}

class GraphiteStatsdConfigLoopbackTest
    : public testing::TestWithParam<Network::Address::IpVersion> {};
INSTANTIATE_TEST_SUITE_P(IpVersions, GraphiteStatsdConfigLoopbackTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(GraphiteStatsdConfigLoopbackTest, ValidUdpIp) {
  const std::string& name = getStatSinkName();

  envoy::extensions::stat_sinks::graphite_statsd::v3::GraphiteStatsdSink sink_config;
  envoy::config::core::v3::Address& address = *sink_config.mutable_address();
  envoy::config::core::v3::SocketAddress& socket_address = *address.mutable_socket_address();
  socket_address.set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  Network::Address::InstanceConstSharedPtr loopback_flavor =
      Network::Test::getCanonicalLoopbackAddress(GetParam());
  socket_address.set_address(loopback_flavor->ip()->addressAsString());
  socket_address.set_port_value(8125);

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  TestUtility::jsonConvert(sink_config, *message);

  NiceMock<Server::Configuration::MockServerFactoryContext> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  EXPECT_NE(sink, nullptr);
  auto udp_sink = dynamic_cast<Common::Statsd::UdpStatsdSink*>(sink.get());
  EXPECT_NE(udp_sink, nullptr);
  EXPECT_EQ(udp_sink->getUseTagForTest(), true);
  EXPECT_EQ(udp_sink->getPrefix(), Common::Statsd::getDefaultPrefix());
}

// Negative test for protoc-gen-validate constraints for graphite_statsd.
TEST(GraphiteStatsdConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockServerFactoryContext> server;
  EXPECT_THROW(
      GraphiteStatsdSinkFactory().createStatsSink(
          envoy::extensions::stat_sinks::graphite_statsd::v3::GraphiteStatsdSink(), server),
      ProtoValidationException);
}

TEST_P(GraphiteStatsdConfigLoopbackTest, CustomBufferSize) {
  const std::string& name = getStatSinkName();

  envoy::extensions::stat_sinks::graphite_statsd::v3::GraphiteStatsdSink sink_config;
  sink_config.mutable_max_bytes_per_datagram()->set_value(128);
  envoy::config::core::v3::Address& address = *sink_config.mutable_address();
  envoy::config::core::v3::SocketAddress& socket_address = *address.mutable_socket_address();
  socket_address.set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  Network::Address::InstanceConstSharedPtr loopback_flavor =
      Network::Test::getCanonicalLoopbackAddress(GetParam());
  socket_address.set_address(loopback_flavor->ip()->addressAsString());
  socket_address.set_port_value(8125);

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  TestUtility::jsonConvert(sink_config, *message);

  NiceMock<Server::Configuration::MockServerFactoryContext> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  ASSERT_NE(sink, nullptr);
  auto udp_sink = dynamic_cast<Common::Statsd::UdpStatsdSink*>(sink.get());
  ASSERT_NE(udp_sink, nullptr);
  EXPECT_EQ(udp_sink->getBufferSizeForTest(), 128);
}

TEST_P(GraphiteStatsdConfigLoopbackTest, DefaultBufferSize) {
  const std::string& name = getStatSinkName();

  envoy::extensions::stat_sinks::graphite_statsd::v3::GraphiteStatsdSink sink_config;
  envoy::config::core::v3::Address& address = *sink_config.mutable_address();
  envoy::config::core::v3::SocketAddress& socket_address = *address.mutable_socket_address();
  socket_address.set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  Network::Address::InstanceConstSharedPtr loopback_flavor =
      Network::Test::getCanonicalLoopbackAddress(GetParam());
  socket_address.set_address(loopback_flavor->ip()->addressAsString());
  socket_address.set_port_value(8125);

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  TestUtility::jsonConvert(sink_config, *message);

  NiceMock<Server::Configuration::MockServerFactoryContext> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  ASSERT_NE(sink, nullptr);
  auto udp_sink = dynamic_cast<Common::Statsd::UdpStatsdSink*>(sink.get());
  ASSERT_NE(udp_sink, nullptr);
  // Expect default buffer size of 0 (no buffering)
  EXPECT_EQ(udp_sink->getBufferSizeForTest(), 0);
}

TEST_P(GraphiteStatsdConfigLoopbackTest, WithCustomPrefix) {
  const std::string& name = getStatSinkName();

  envoy::extensions::stat_sinks::graphite_statsd::v3::GraphiteStatsdSink sink_config;
  envoy::config::core::v3::Address& address = *sink_config.mutable_address();
  envoy::config::core::v3::SocketAddress& socket_address = *address.mutable_socket_address();
  socket_address.set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  Network::Address::InstanceConstSharedPtr loopback_flavor =
      Network::Test::getCanonicalLoopbackAddress(GetParam());
  socket_address.set_address(loopback_flavor->ip()->addressAsString());
  socket_address.set_port_value(8125);

  const std::string customPrefix = "prefix.test";
  sink_config.set_prefix(customPrefix);

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  TestUtility::jsonConvert(sink_config, *message);

  NiceMock<Server::Configuration::MockServerFactoryContext> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  ASSERT_NE(sink, nullptr);
  auto udp_sink = dynamic_cast<Common::Statsd::UdpStatsdSink*>(sink.get());
  ASSERT_NE(udp_sink, nullptr);
  EXPECT_EQ(udp_sink->getPrefix(), customPrefix);
}

} // namespace
} // namespace GraphiteStatsd
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
