#include "envoy/extensions/udp_packet_writer/v3/udp_gso_batch_writer_factory.pb.h"

#include "source/extensions/udp_packet_writer/gso/config.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

TEST(FactoryTest, Name) {
  UdpGsoBatchWriterFactoryFactory factory;
  EXPECT_EQ(factory.name(), "envoy.udp_packet_writer.gso");
}

TEST(FactoryTest, CreateEmptyConfigProto) {
  UdpGsoBatchWriterFactoryFactory factory;
  EXPECT_TRUE(factory.createEmptyConfigProto() != nullptr);
}

TEST(FactoryTest, CreateUdpPacketWriterFactory) {
  UdpGsoBatchWriterFactoryFactory factory;
  envoy::extensions::udp_packet_writer::v3::UdpGsoBatchWriterFactory writer_config;
  envoy::config::core::v3::TypedExtensionConfig config;
  config.mutable_typed_config()->PackFrom(writer_config);
  EXPECT_TRUE(factory.createUdpPacketWriterFactory(config) != nullptr);
}

} // namespace Quic
} // namespace Envoy
