#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/socket.h"

#include "quiche/quic/core/connection_id_generator.h"
#include "quiche/quic/load_balancer/load_balancer_encoder.h"

namespace Envoy {
namespace Quic {

using QuicConnectionIdGeneratorPtr = std::unique_ptr<quic::ConnectionIdGeneratorInterface>;

/**
 * A factory interface to provide Quic connection IDs and compatible BPF code for stable packet
 * routing.
 */
class EnvoyQuicConnectionIdGeneratorFactory {
public:
  virtual ~EnvoyQuicConnectionIdGeneratorFactory() {}

  virtual QuicConnectionIdGeneratorPtr
  createQuicConnectionIdGenerator(quic::LoadBalancerEncoder& lb_encoder,
                                  uint32_t worker_index) PURE;

  /**
   * the length of connection IDs to be generated when there is no active config
   */
  virtual uint8_t getConnectionIdLengthWithoutRouteConfig() PURE;

  /**
   * Returns true if the connection ID length should be encoded in first byte of the generated
   * connection ID.
   */
  virtual bool firstByteEncodesConnectionIdLength() PURE;

  /**
   * Create a socket option with BPF program to consistently route QUIC packets to the right listen
   * socket. Linux only.
   */
  virtual Network::Socket::OptionConstSharedPtr
  createCompatibleLinuxBpfSocketOption(uint32_t concurrency) PURE;
};

using EnvoyQuicConnectionIdGeneratorFactoryPtr =
    std::unique_ptr<EnvoyQuicConnectionIdGeneratorFactory>;

class EnvoyQuicConnectionIdGeneratorConfigFactory : public Config::TypedFactory {
public:
  std::string category() const override { return "envoy.quic.connection_id_generator"; }

  /**
   * Returns a connection ID factory based on the given config.
   */
  virtual EnvoyQuicConnectionIdGeneratorFactoryPtr
  createQuicConnectionIdGeneratorFactory(const Protobuf::Message& config) PURE;
};

} // namespace Quic
} // namespace Envoy
