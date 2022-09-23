#include "source/common/quic/envoy_deterministic_connection_id_generator.h"

#include "source/common/quic/envoy_quic_utils.h"

namespace Envoy {
namespace Quic {

absl::optional<quic::QuicConnectionId>
EnvoyDeterministicConnectionIdGenerator::GenerateNextConnectionId(
    const quic::QuicConnectionId& original) {
  auto new_cid = DeterministicConnectionIdGenerator::GenerateNextConnectionId(original);
  if (new_cid.has_value()) {
    adjustNewConnectionIdForRoutine(new_cid.value(), original);
  }
  return new_cid;
}

absl::optional<quic::QuicConnectionId>
EnvoyDeterministicConnectionIdGenerator::MaybeReplaceConnectionId(
    const quic::QuicConnectionId& original, const quic::ParsedQuicVersion& version) {
  auto new_cid = DeterministicConnectionIdGenerator::MaybeReplaceConnectionId(original, version);
  if (new_cid.has_value()) {
    adjustNewConnectionIdForRoutine(new_cid.value(), original);
  }
  return new_cid;
}

} // namespace Quic
} // namespace Envoy
