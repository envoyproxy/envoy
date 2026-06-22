#pragma once

#include "third_party/envoy/src/api/envoy/config/core/v3/extension.pb.h"
#include "third_party/envoy/src/envoy/config/typed_config.h"
#include "third_party/envoy/src/envoy/server/factory_context.h"
#include "third_party/envoy/src/source/common/quic/envoy_quic_packet_writer.h"

namespace Envoy {
namespace Quic {

class QuicPacketWriterFactoryFactory : public Envoy::Config::TypedFactory {
 public:
  ~QuicPacketWriterFactoryFactory() override = default;

  virtual QuicPacketWriterFactoryPtr createQuicPacketWriterFactory(
      const envoy::config::core::v3::TypedExtensionConfig& config,
      Server::Configuration::ListenerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.quic.packet_writer"; }
};

}  // namespace Quic
}  // namespace Envoy
