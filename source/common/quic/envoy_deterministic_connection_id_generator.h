#pragma once

#include "quiche/quic/core/deterministic_connection_id_generator.h"

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

} // namespace Quic
} // namespace Envoy
