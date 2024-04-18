#pragma once

#include "envoy/extensions/udp_packet_writer/v3/udp_default_writer_factory.pb.h"
#include "envoy/network/udp_packet_writer_handler.h"
#include "envoy/registry/registry.h"

#include "source/common/network/udp_packet_writer_handler_impl.h"

namespace Envoy {
namespace Network {

class UdpDefaultWriterFactoryFactory : public Network::UdpPacketWriterFactoryFactory {
public:
  std::string name() const override { return "envoy.udp_packet_writer.default"; }
  UdpPacketWriterFactoryPtr
  createUdpPacketWriterFactory(const envoy::config::core::v3::TypedExtensionConfig&) override {
    return std::make_unique<UdpDefaultWriterFactory>();
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::udp_packet_writer::v3::UdpDefaultWriterFactory>();
  }
};

DECLARE_FACTORY(UdpDefaultWriterFactoryFactory);

} // namespace Network
} // namespace Envoy
