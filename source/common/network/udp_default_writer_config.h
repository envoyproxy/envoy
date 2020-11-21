#pragma once

#include "envoy/network/udp_packet_writer_config.h"
#include "envoy/network/udp_packet_writer_handler.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Network {

class UdpDefaultWriterFactory : public Network::UdpPacketWriterFactory {
public:
  Network::UdpPacketWriterPtr createUdpPacketWriter(Network::IoHandle& io_handle,
                                                    Stats::Scope& scope) override;
};

// UdpPacketWriterConfigFactory to create UdpDefaultWriterFactory based on given protobuf
// This is the default UdpPacketWriterConfigFactory if not specified in config.
class UdpDefaultWriterConfigFactory : public UdpPacketWriterConfigFactory {
public:
  // UdpPacketWriterConfigFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  Network::UdpPacketWriterFactoryPtr
  createUdpPacketWriterFactory(const Protobuf::Message&) override;

  std::string name() const override;
};

DECLARE_FACTORY(UdpDefaultWriterConfigFactory);

} // namespace Network
} // namespace Envoy
