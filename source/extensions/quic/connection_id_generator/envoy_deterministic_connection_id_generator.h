#pragma once

#include "source/common/quic/envoy_quic_connection_id_generator_factory.h"

#include "quiche/quic/core/deterministic_connection_id_generator.h"

#if defined(__linux__)
#include <linux/filter.h>
#endif

namespace Envoy {
namespace Quic {

// This class modifies connection ids that are too long in an Envoy fashion.
class EnvoyDeterministicConnectionIdGenerator : public quic::DeterministicConnectionIdGenerator {

  using DeterministicConnectionIdGenerator::DeterministicConnectionIdGenerator;

public:
  // Hashes |original| to create a new connection ID in Envoy fashion.
  absl::optional<quic::QuicConnectionId>
  GenerateNextConnectionId(const quic::QuicConnectionId& original) override;
  // Replace the connection ID if and only if |original| is not of the expected
  // length in Envoy fashion.
  absl::optional<quic::QuicConnectionId>
  MaybeReplaceConnectionId(const quic::QuicConnectionId& original,
                           const quic::ParsedQuicVersion& version) override;
};

class EnvoyDeterministicConnectionIdGeneratorFactory
    : public EnvoyQuicConnectionIdGeneratorFactory {
public:
  // EnvoyQuicConnectionIdGeneratorFactory.
  QuicConnectionIdGeneratorPtr createQuicConnectionIdGenerator(uint32_t worker_index) override;
  Network::Socket::OptionConstSharedPtr
  createCompatibleLinuxBpfSocketOption(uint32_t concurrency) override;

private:
#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
  sock_fprog prog_;
  std::vector<sock_filter> filter_;
#endif
};

} // namespace Quic
} // namespace Envoy
