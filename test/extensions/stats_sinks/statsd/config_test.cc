#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/network/address.h"
#include "envoy/registry/registry.h"

#include "source/common/config/well_known_names.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/stat_sinks/common/statsd/statsd.h"
#include "source/extensions/stat_sinks/statsd/config.h"

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
namespace Statsd {
namespace {

TEST(StatsConfigTest, ValidTcpStatsd) {
  envoy::config::metrics::v3::StatsdSink sink_config;
  sink_config.set_tcp_cluster_name("fake_cluster");

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(StatsdName);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  TestUtility::jsonConvert(sink_config, *message);

  NiceMock<Server::Configuration::MockServerFactoryContext> server;
  server.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  EXPECT_NE(sink, nullptr);
  EXPECT_NE(dynamic_cast<Common::Statsd::TcpStatsdSink*>(sink.get()), nullptr);
}

class StatsConfigParameterizedTest : public testing::TestWithParam<Network::Address::IpVersion> {};

INSTANTIATE_TEST_SUITE_P(IpVersions, StatsConfigParameterizedTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(StatsConfigParameterizedTest, UdpSinkDefaultPrefix) {
  const auto& defaultPrefix = Common::Statsd::getDefaultPrefix();

  envoy::config::metrics::v3::StatsdSink sink_config;
  envoy::config::core::v3::Address& address = *sink_config.mutable_address();
  envoy::config::core::v3::SocketAddress& socket_address = *address.mutable_socket_address();
  socket_address.set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  if (GetParam() == Network::Address::IpVersion::v4) {
    socket_address.set_address("127.0.0.1");
  } else {
    socket_address.set_address("::1");
  }
  socket_address.set_port_value(8125);
  EXPECT_EQ(sink_config.prefix(), "");

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(StatsdName);
  ASSERT_NE(factory, nullptr);
  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  TestUtility::jsonConvert(sink_config, *message);

  NiceMock<Server::Configuration::MockServerFactoryContext> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  ASSERT_NE(sink, nullptr);

  auto udp_sink = dynamic_cast<Common::Statsd::UdpStatsdSink*>(sink.get());
  ASSERT_NE(udp_sink, nullptr);
  EXPECT_EQ(udp_sink->getPrefix(), defaultPrefix);
}

TEST_P(StatsConfigParameterizedTest, UdpSinkCustomPrefix) {
  const std::string customPrefix = "prefix.test";

  envoy::config::metrics::v3::StatsdSink sink_config;
  envoy::config::core::v3::Address& address = *sink_config.mutable_address();
  envoy::config::core::v3::SocketAddress& socket_address = *address.mutable_socket_address();
  socket_address.set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  if (GetParam() == Network::Address::IpVersion::v4) {
    socket_address.set_address("127.0.0.1");
  } else {
    socket_address.set_address("::1");
  }
  socket_address.set_port_value(8125);
  sink_config.set_prefix(customPrefix);
  EXPECT_NE(sink_config.prefix(), "");

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(StatsdName);
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

TEST(StatsConfigTest, TcpSinkDefaultPrefix) {
  envoy::config::metrics::v3::StatsdSink sink_config;
  const auto& defaultPrefix = Common::Statsd::getDefaultPrefix();
  sink_config.set_tcp_cluster_name("fake_cluster");

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(StatsdName);
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(sink_config.prefix(), "");
  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  TestUtility::jsonConvert(sink_config, *message);

  NiceMock<Server::Configuration::MockServerFactoryContext> server;
  server.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  ASSERT_NE(sink, nullptr);

  auto tcp_sink = dynamic_cast<Common::Statsd::TcpStatsdSink*>(sink.get());
  ASSERT_NE(tcp_sink, nullptr);
  EXPECT_EQ(tcp_sink->getPrefix(), defaultPrefix);
}

TEST(StatsConfigTest, TcpSinkCustomPrefix) {
  envoy::config::metrics::v3::StatsdSink sink_config;
  std::string prefix = "prefixTest";
  sink_config.set_tcp_cluster_name("fake_cluster");
  ASSERT_NE(sink_config.prefix(), prefix);
  sink_config.set_prefix(prefix);
  EXPECT_EQ(sink_config.prefix(), prefix);
  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(StatsdName);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  TestUtility::jsonConvert(sink_config, *message);

  NiceMock<Server::Configuration::MockServerFactoryContext> server;
  server.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  ASSERT_NE(sink, nullptr);

  auto tcp_sink = dynamic_cast<Common::Statsd::TcpStatsdSink*>(sink.get());
  ASSERT_NE(tcp_sink, nullptr);
  EXPECT_EQ(tcp_sink->getPrefix(), prefix);
}

class StatsConfigLoopbackTest : public testing::TestWithParam<Network::Address::IpVersion> {};
INSTANTIATE_TEST_SUITE_P(IpVersions, StatsConfigLoopbackTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(StatsConfigLoopbackTest, ValidUdpIpStatsd) {
  envoy::config::metrics::v3::StatsdSink sink_config;
  envoy::config::core::v3::Address& address = *sink_config.mutable_address();
  envoy::config::core::v3::SocketAddress& socket_address = *address.mutable_socket_address();
  socket_address.set_protocol(envoy::config::core::v3::SocketAddress::UDP);
  auto loopback_flavor = Network::Test::getCanonicalLoopbackAddress(GetParam());
  socket_address.set_address(loopback_flavor->ip()->addressAsString());
  socket_address.set_port_value(8125);

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(StatsdName);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  TestUtility::jsonConvert(sink_config, *message);

  NiceMock<Server::Configuration::MockServerFactoryContext> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  EXPECT_NE(sink, nullptr);
  EXPECT_NE(dynamic_cast<Common::Statsd::UdpStatsdSink*>(sink.get()), nullptr);
  EXPECT_EQ(dynamic_cast<Common::Statsd::UdpStatsdSink*>(sink.get())->getUseTagForTest(), false);
}

// Negative test for protoc-gen-validate constraints for statsd.
TEST(StatsdConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockServerFactoryContext> server;
  EXPECT_THROW(
      StatsdSinkFactory().createStatsSink(envoy::config::metrics::v3::StatsdSink(), server),
      ProtoValidationException);
}

} // namespace
} // namespace Statsd
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
