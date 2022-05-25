#include "envoy/extensions/udp_packet_writer/v3/udp_default_writer_factory.pb.h"

#include "source/extensions/udp_packet_writer/default/config.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

TEST(FactoryTest, Name) {
  Network::UdpDefaultWriterFactoryFactory factory;
  EXPECT_EQ(factory.name(), "envoy.udp_packet_writer.default");
}

TEST(FactoryTest, CreateEmptyConfigProto) {
  Network::UdpDefaultWriterFactoryFactory factory;
  EXPECT_TRUE(factory.createEmptyConfigProto() != nullptr);
}

TEST(FactoryTest, CreateUdpPacketWriterFactory) {
  Network::UdpDefaultWriterFactoryFactory factory;
  envoy::extensions::udp_packet_writer::v3::UdpDefaultWriterFactory writer_config;
  envoy::config::core::v3::TypedExtensionConfig config;
  config.mutable_typed_config()->PackFrom(writer_config);
  EXPECT_TRUE(factory.createUdpPacketWriterFactory(config) != nullptr);
}

} // namespace Quic
} // namespace Envoy
