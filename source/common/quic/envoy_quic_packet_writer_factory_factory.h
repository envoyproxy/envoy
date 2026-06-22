#pragma once

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"

#include "source/common/quic/envoy_quic_packet_writer.h"

namespace Envoy {
namespace Quic {

class QuicPacketWriterFactoryFactory : public Envoy::Config::TypedFactory {
public:
  ~QuicPacketWriterFactoryFactory() override = default;

  virtual QuicPacketWriterFactoryPtr
  createQuicPacketWriterFactory(const envoy::config::core::v3::TypedExtensionConfig& config,
                                Server::Configuration::ListenerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.quic.packet_writer"; }
};

} // namespace Quic
} // namespace Envoy
