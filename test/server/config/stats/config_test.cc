#include <string>

#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"
#include "common/stats/statsd.h"

#include "server/config/stats/statsd.h"

#include "test/mocks/server/mocks.h"

#include "api/bootstrap.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Server {
namespace Configuration {

TEST(StatsConfigTest, ValidTcpStatsd) {
  const std::string name = "envoy.statsd";
  Protobuf::Struct config;
  ProtobufWkt::Map<ProtobufTypes::String, ProtobufWkt::Value>& field_map = *config.mutable_fields();
  field_map["tcp_cluster_name"].set_string_value("fake_cluster");

  StatsSinkFactory* factory = Registry::FactoryRegistry<StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  MessageUtil::jsonConvert(config, *message);

  NiceMock<MockInstance> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server, server.clusterManager());
  EXPECT_NE(sink, nullptr);
  EXPECT_NE(dynamic_cast<Stats::Statsd::TcpStatsdSink*>(sink.get()), nullptr);
}

TEST(StatsConfigTest, ValidUdpIpStatsd) {
  const std::string name = "envoy.statsd";
  Protobuf::Struct config;
  ProtobufWkt::Map<ProtobufTypes::String, ProtobufWkt::Value>& field_map = *config.mutable_fields();

  envoy::api::v2::Address address;
  envoy::api::v2::SocketAddress& socket_address = *address.mutable_socket_address();
  socket_address.set_protocol(envoy::api::v2::SocketAddress::UDP);
  socket_address.set_address("127.0.0.1");
  socket_address.set_port_value(8125);

  ProtobufWkt::Map<ProtobufTypes::String, ProtobufWkt::Value>& address_field_map =
      *field_map["address"].mutable_struct_value()->mutable_fields();
  ProtobufWkt::Map<ProtobufTypes::String, ProtobufWkt::Value>& socket_address_field_map =
      *address_field_map["socket_address"].mutable_struct_value()->mutable_fields();
  socket_address_field_map["protocol"].set_string_value("UDP");
  socket_address_field_map["address"].set_string_value("127.0.0.1");
  socket_address_field_map["port_value"].set_number_value(8125);

  StatsSinkFactory* factory = Registry::FactoryRegistry<StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  MessageUtil::jsonConvert(config, *message);

  // REMOVE THIS
  auto& sink_proto = dynamic_cast<envoy::api::v2::StatsdSink&>(*message);
  ASSERT_EQ(envoy::api::v2::SocketAddress::UDP, sink_proto.address().socket_address().protocol());
  ASSERT_EQ("127.0.0.1", sink_proto.address().socket_address().address());
  ASSERT_EQ(8125, sink_proto.address().socket_address().port_value());

  NiceMock<MockInstance> server;
  Stats::SinkPtr sink = factory->createStatsSink(*message, server, server.clusterManager());
  EXPECT_NE(sink, nullptr);
  EXPECT_NE(dynamic_cast<Stats::Statsd::UdpStatsdSink*>(sink.get()), nullptr);
}

TEST(StatsConfigTest, EmptyConfig) {
  const std::string name = "envoy.statsd";
  Protobuf::Struct config;

  StatsSinkFactory* factory = Registry::FactoryRegistry<StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  MessageUtil::jsonConvert(config, *message);
  NiceMock<MockInstance> server;
  EXPECT_THROW(factory->createStatsSink(*message, server, server.clusterManager()), EnvoyException);
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
