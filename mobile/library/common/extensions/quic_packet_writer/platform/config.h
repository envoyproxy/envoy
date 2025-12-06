#pragma once

#include "envoy/registry/registry.h"

#include "source/common/quic/envoy_quic_client_packet_writer_factory.h"

#include "library/common/extensions/quic_packet_writer/platform/platform_packet_writer.pb.h"

namespace Envoy {
namespace Quic {

class QuicPlatformPacketWriterConfigFactory
    : public Envoy::Quic::QuicClientPacketWriterConfigFactory {
public:
  std::string name() const override { return "envoy.quic.packet_writer.platform"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy_mobile::extensions::quic_packet_writer::platform::QuicPlatformPacketWriterConfig>();
  }

  Envoy::Quic::QuicClientPacketWriterFactoryPtr createQuicClientPacketWriterFactory(
      const Protobuf::Message& config,
      Envoy::ProtobufMessage::ValidationVisitor& validation_visitor) override;
};

DECLARE_FACTORY(QuicPlatformPacketWriterConfigFactory);

} // namespace Quic
} // namespace Envoy
