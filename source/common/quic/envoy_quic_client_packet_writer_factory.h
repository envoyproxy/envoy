#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/address.h"
#include "envoy/network/listen_socket.h"
#include "envoy/server/factory_context.h"

#include "source/common/quic/envoy_quic_packet_writer.h"

#include "quiche/quic/core/quic_path_validator.h"

namespace Envoy {
namespace Quic {

class QuicClientPacketWriterFactory {
public:
  virtual ~QuicClientPacketWriterFactory() = default;

  struct CreationResult {
    std::unique_ptr<EnvoyQuicPacketWriter> writer_;
    Network::ConnectionSocketPtr socket_;
  };

  /**
   * Creates a socket and a QUIC packet writer associated with it.
   * @param server_addr The server address to connect to.
   * @param network The network to bind the socket to.
   * @param local_addr The local address to bind if not nullptr and if the network is invalid. Will
   * be set to the actual local address of the created socket.
   * @param options The socket options to apply.
   * @return A struct containing the created socket and writer objects.
   */
  virtual CreationResult
  createSocketAndQuicPacketWriter(Network::Address::InstanceConstSharedPtr server_addr,
                                  quic::QuicNetworkHandle network,
                                  Network::Address::InstanceConstSharedPtr& local_addr,
                                  const Network::ConnectionSocket::OptionsSharedPtr& options) PURE;
};

using QuicClientPacketWriterFactoryPtr = std::shared_ptr<QuicClientPacketWriterFactory>;

class QuicClientPacketWriterConfigFactory : public Config::TypedFactory {
public:
  std::string category() const override { return "envoy.quic.packet_writer"; }

  /**
   * Returns a packet writer factory based on the given config.
   */
  virtual QuicClientPacketWriterFactoryPtr
  createQuicClientPacketWriterFactory(const Protobuf::Message& config,
                                      ProtobufMessage::ValidationVisitor& validation_visitor) PURE;
};

} // namespace Quic
} // namespace Envoy
