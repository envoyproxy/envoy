#pragma once

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/network/udp_packet_writer_handler.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Network {

/**
 * UdpPacketWriterFactoryFactory adds an extra layer of indirection In order to
 * support a UdpPacketWriterFactory whose behavior depends on the
 * TypedConfig for that factory. The UdpPacketWriterFactoryFactory is created
 * with a no-arg constructor based on the type of the config. Then this
 * `createUdpPacketWriterFactory` can be called with the config to
 * create an actual UdpPacketWriterFactory.
 */
class UdpPacketWriterFactoryFactory : public Envoy::Config::TypedFactory {
public:
  ~UdpPacketWriterFactoryFactory() override = default;

  /**
   * Creates an UdpPacketWriterFactory based on the specified config.
   * @return the UdpPacketWriterFactory created.
   */
  virtual UdpPacketWriterFactoryPtr
  createUdpPacketWriterFactory(const envoy::config::core::v3::TypedExtensionConfig& config,
                               Server::Configuration::ListenerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.udp_packet_writer"; }
};

} // namespace Network
} // namespace Envoy
