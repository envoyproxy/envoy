#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/socket.h"

#include "quiche/quic/core/connection_id_generator.h"
#include "quiche/quic/load_balancer/load_balancer_encoder.h"

namespace Envoy {
namespace Quic {

using QuicConnectionIdGeneratorPtr = std::unique_ptr<quic::ConnectionIdGeneratorInterface>;

/**
 * A factory interface to provide QUIC connection IDs and compatible BPF code for stable packet
 * routing.
 */
class EnvoyQuicConnectionIdGeneratorFactory {
public:
  virtual ~EnvoyQuicConnectionIdGeneratorFactory() = default;

  /**
   * Create a connection ID generator object.
   * @param worker_index an index to be encoded to QUIC connection ID for routing packets to the
   * current listener.
   */
  virtual QuicConnectionIdGeneratorPtr createQuicConnectionIdGenerator(uint32_t worker_index) PURE;

  /**
   * Create a socket option with BPF program to consistently route QUIC packets to the right listen
   * socket. Linux only.
   * @param concurrency the total number of worker threads.
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
