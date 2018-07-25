#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/network/address.h"
#include "envoy/registry/registry.h"

#include "common/config/well_known_names.h"
#include "common/protobuf/utility.h"

#include "extensions/stat_sinks/common/statsd/statsd.h"
#include "extensions/stat_sinks/statsd/config.h"
#include "extensions/stat_sinks/well_known_names.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Statsd {

TEST(StatsConfigTest, ValidTcpStatsd) {
  const std::string name = StatsSinkNames::get().Statsd;

  envoy::config::metrics::v2::StatsdSink sink_config;
  sink_config.set_tcp_cluster_name("fake_cluster");

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  MessageUtil::jsonConvert(sink_config, *message);

  NiceMock<Server::MockInstance> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  EXPECT_NE(sink, nullptr);
  EXPECT_NE(dynamic_cast<Common::Statsd::TcpStatsdSink*>(sink.get()), nullptr);
}

class StatsConfigParameterizedTest : public testing::TestWithParam<Network::Address::IpVersion> {};

INSTANTIATE_TEST_CASE_P(IpVersions, StatsConfigParameterizedTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(StatsConfigParameterizedTest, UdpSinkDefaultPrefix) {
  const std::string name = StatsSinkNames::get().Statsd;
  auto defaultPrefix = Common::Statsd::getDefaultPrefix();

  envoy::config::metrics::v2::StatsdSink sink_config;
  envoy::api::v2::core::Address& address = *sink_config.mutable_address();
  envoy::api::v2::core::SocketAddress& socket_address = *address.mutable_socket_address();
  socket_address.set_protocol(envoy::api::v2::core::SocketAddress::UDP);
  if (GetParam() == Network::Address::IpVersion::v4) {
    socket_address.set_address("127.0.0.1");
  } else {
    socket_address.set_address("::1");
  }
  socket_address.set_port_value(8125);
  EXPECT_EQ(sink_config.prefix(), "");

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);
  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  MessageUtil::jsonConvert(sink_config, *message);

  NiceMock<Server::MockInstance> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  ASSERT_NE(sink, nullptr);

  auto udp_sink = dynamic_cast<Common::Statsd::UdpStatsdSink*>(sink.get());
  ASSERT_NE(udp_sink, nullptr);
  EXPECT_EQ(udp_sink->getPrefix(), defaultPrefix);
}

TEST_P(StatsConfigParameterizedTest, UdpSinkCustomPrefix) {
  const std::string name = StatsSinkNames::get().Statsd;
  const std::string customPrefix = "prefix.test";

  envoy::config::metrics::v2::StatsdSink sink_config;
  envoy::api::v2::core::Address& address = *sink_config.mutable_address();
  envoy::api::v2::core::SocketAddress& socket_address = *address.mutable_socket_address();
  socket_address.set_protocol(envoy::api::v2::core::SocketAddress::UDP);
  if (GetParam() == Network::Address::IpVersion::v4) {
    socket_address.set_address("127.0.0.1");
  } else {
    socket_address.set_address("::1");
  }
  socket_address.set_port_value(8125);
  sink_config.set_prefix(customPrefix);
  EXPECT_NE(sink_config.prefix(), "");

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);
  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  MessageUtil::jsonConvert(sink_config, *message);

  NiceMock<Server::MockInstance> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  ASSERT_NE(sink, nullptr);

  auto udp_sink = dynamic_cast<Common::Statsd::UdpStatsdSink*>(sink.get());
  ASSERT_NE(udp_sink, nullptr);
  EXPECT_EQ(udp_sink->getPrefix(), customPrefix);
}

TEST(StatsConfigTest, TcpSinkDefaultPrefix) {
  const std::string name = StatsSinkNames::get().Statsd;

  envoy::config::metrics::v2::StatsdSink sink_config;
  auto defaultPrefix = Common::Statsd::getDefaultPrefix();
  sink_config.set_tcp_cluster_name("fake_cluster");

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(sink_config.prefix(), "");
  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  MessageUtil::jsonConvert(sink_config, *message);

  NiceMock<Server::MockInstance> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  ASSERT_NE(sink, nullptr);

  auto tcp_sink = dynamic_cast<Common::Statsd::TcpStatsdSink*>(sink.get());
  ASSERT_NE(tcp_sink, nullptr);
  EXPECT_EQ(tcp_sink->getPrefix(), defaultPrefix);
}

TEST(StatsConfigTest, TcpSinkCustomPrefix) {
  const std::string name = StatsSinkNames::get().Statsd;

  envoy::config::metrics::v2::StatsdSink sink_config;
  ProtobufTypes::String prefix = "prefixTest";
  sink_config.set_tcp_cluster_name("fake_cluster");
  ASSERT_NE(sink_config.prefix(), prefix);
  sink_config.set_prefix(prefix);
  EXPECT_EQ(sink_config.prefix(), prefix);
  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  MessageUtil::jsonConvert(sink_config, *message);

  NiceMock<Server::MockInstance> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  ASSERT_NE(sink, nullptr);

  auto tcp_sink = dynamic_cast<Common::Statsd::TcpStatsdSink*>(sink.get());
  ASSERT_NE(tcp_sink, nullptr);
  EXPECT_EQ(tcp_sink->getPrefix(), prefix);
}

class StatsConfigLoopbackTest : public testing::TestWithParam<Network::Address::IpVersion> {};
INSTANTIATE_TEST_CASE_P(IpVersions, StatsConfigLoopbackTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(StatsConfigLoopbackTest, ValidUdpIpStatsd) {
  const std::string name = StatsSinkNames::get().Statsd;

  envoy::config::metrics::v2::StatsdSink sink_config;
  envoy::api::v2::core::Address& address = *sink_config.mutable_address();
  envoy::api::v2::core::SocketAddress& socket_address = *address.mutable_socket_address();
  socket_address.set_protocol(envoy::api::v2::core::SocketAddress::UDP);
  auto loopback_flavor = Network::Test::getCanonicalLoopbackAddress(GetParam());
  socket_address.set_address(loopback_flavor->ip()->addressAsString());
  socket_address.set_port_value(8125);

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  MessageUtil::jsonConvert(sink_config, *message);

  NiceMock<Server::MockInstance> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server);
  EXPECT_NE(sink, nullptr);
  EXPECT_NE(dynamic_cast<Common::Statsd::UdpStatsdSink*>(sink.get()), nullptr);
  EXPECT_EQ(dynamic_cast<Common::Statsd::UdpStatsdSink*>(sink.get())->getUseTagForTest(), false);
}

// Negative test for protoc-gen-validate constraints for statsd.
TEST(StatsdConfigTest, ValidateFail) {
  NiceMock<Server::MockInstance> server;
  EXPECT_THROW(
      StatsdSinkFactory().createStatsSink(envoy::config::metrics::v2::StatsdSink(), server),
      ProtoValidationException);
}

} // namespace Statsd
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
