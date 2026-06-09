#pragma once

#include "envoy/extensions/quic/client_writer_factory/v3/default_client_writer.pb.h"
#include "envoy/extensions/quic/client_writer_factory/v3/default_client_writer.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/quic/envoy_quic_client_packet_writer_factory.h"
#include "source/common/quic/quic_client_packet_writer_factory_impl.h"

namespace Envoy {
namespace Quic {

class DefaultQuicClientPacketWriterFactoryConfig : public QuicClientPacketWriterConfigFactory {
public:
  std::string name() const override { return "envoy.quic.packet_writer.default"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::quic::client_writer_factory::v3::DefaultClientWriter>();
  }

  QuicClientPacketWriterFactoryPtr createQuicClientPacketWriterFactory(
      const Protobuf::Message& /*config*/, Event::Dispatcher& /*dispatcher*/,
      ProtobufMessage::ValidationVisitor& /*validation_visitor*/) override {
    return std::make_unique<QuicClientPacketWriterFactoryImpl>();
  }
};

DECLARE_FACTORY(DefaultQuicClientPacketWriterFactoryConfig);

} // namespace Quic
} // namespace Envoy
