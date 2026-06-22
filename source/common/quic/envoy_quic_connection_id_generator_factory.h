#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/network/socket.h"
#include "envoy/server/factory_context.h"

#include "quiche/quic/core/connection_id_generator.h"

namespace Envoy {
namespace Quic {

using QuicConnectionIdGeneratorPtr = std::unique_ptr<quic::ConnectionIdGeneratorInterface>;
// A function similar to the BPF program from createCompatibleLinuxBpfSocketOption, it takes
// a QUIC packet and returns the appropriate worker_index.
using QuicConnectionIdWorkerSelector =
    std::function<uint32_t(const Buffer::Instance& packet, uint32_t default_value)>;

/**
 * A factory interface to provide QUIC connection IDs and compatible BPF code for stable packet
 * routing.
 */
class EnvoyQuicConnectionIdGeneratorContext {
public:
  virtual ~EnvoyQuicConnectionIdGeneratorContext() = default;

  /**
   * Create a connection ID generator object.
   * @param worker_index an index to be encoded to QUIC connection ID for routing packets to the
   * current listener.
   */
  virtual QuicConnectionIdGeneratorPtr createQuicConnectionIdGenerator(uint32_t worker_index) PURE;

  /**
   * Create a socket option with BPF program to consistently route QUIC packets to the right listen
   * socket. Linux only, absl::UnimplementedError on other platforms.
   * @param concurrency the total number of worker threads.
   * @returns the socket option or an error status.
   */
  virtual absl::StatusOr<Network::Socket::OptionConstSharedPtr>
  createCompatibleLinuxBpfSocketOption(uint32_t concurrency) PURE;

  /**
   * Returns a function to retrieve the worker index associated with a QUIC packet; the same
   * principle as the BPF program above, but for contexts where BPF is unavailable.
   */
  virtual QuicConnectionIdWorkerSelector
  getCompatibleConnectionIdWorkerSelector(uint32_t concurrency) PURE;
};

using EnvoyQuicConnectionIdGeneratorContextPtr =
    std::unique_ptr<EnvoyQuicConnectionIdGeneratorContext>;

class EnvoyQuicConnectionIdGeneratorFactory {
public:
  virtual ~EnvoyQuicConnectionIdGeneratorFactory() = default;

  virtual EnvoyQuicConnectionIdGeneratorContextPtr createQuicConnectionIdGeneratorContext() PURE;
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
  createQuicConnectionIdGeneratorFactory(const Protobuf::Message& config,
                                         ProtobufMessage::ValidationVisitor& validation_visitor,
                                         Server::Configuration::FactoryContext& context) PURE;
};

} // namespace Quic
} // namespace Envoy
