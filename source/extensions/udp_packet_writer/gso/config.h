#pragma once

#include "envoy/extensions/udp_packet_writer/v3/udp_gso_batch_writer_factory.pb.h"
#include "envoy/network/udp_packet_writer_handler.h"
#include "envoy/registry/registry.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/udp_gso_batch_writer.h"
#endif

#if UDP_GSO_BATCH_WRITER_COMPILETIME_SUPPORT

namespace Envoy {
namespace Quic {

class UdpGsoBatchWriterFactoryFactory : public Network::UdpPacketWriterFactoryFactory {
public:
  std::string name() const override { return "envoy.udp_packet_writer.gso"; }
  Network::UdpPacketWriterFactoryPtr
  createUdpPacketWriterFactory(const envoy::config::core::v3::TypedExtensionConfig&) override {
#ifdef ENVOY_ENABLE_QUIC
    return std::make_unique<UdpGsoBatchWriterFactory>();
#else
    return {};
#endif
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::udp_packet_writer::v3::UdpGsoBatchWriterFactory>();
  }

private:
  envoy::config::core::v3::RuntimeFeatureFlag enabled_;
};

DECLARE_FACTORY(UdpGsoBatchWriterFactoryFactory);

} // namespace Quic
} // namespace Envoy

#endif
