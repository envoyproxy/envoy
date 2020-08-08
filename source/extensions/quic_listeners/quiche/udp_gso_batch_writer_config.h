#pragma once

#include <string>

#include "envoy/network/udp_packet_writer_config.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Quic {

const std::string GsoBatchWriterName{"udp_gso_batch_writer"};

// Network::UdpPacketWriterConfigFactory to create UdpGsoBatchWriterFactory based on given
// protobuf.
class UdpGsoBatchWriterConfigFactory : public Network::UdpPacketWriterConfigFactory {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  Network::UdpPacketWriterFactoryPtr
  createUdpPacketWriterFactory(const Protobuf::Message&) override;

  std::string name() const override;
};

DECLARE_FACTORY(UdpGsoBatchWriterConfigFactory);

} // namespace Quic
} // namespace Envoy
