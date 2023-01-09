#include "source/extensions/quic/connection_id_generator/envoy_deterministic_connection_id_generator.h"

#include "source/common/network/socket_option_impl.h"
#include "source/common/quic/envoy_quic_utils.h"

#include "quiche/quic/load_balancer/load_balancer_encoder.h"

namespace Envoy {
namespace Quic {

absl::optional<quic::QuicConnectionId>
EnvoyDeterministicConnectionIdGenerator::GenerateNextConnectionId(
    const quic::QuicConnectionId& original) {
  auto new_cid = DeterministicConnectionIdGenerator::GenerateNextConnectionId(original);
  if (new_cid.has_value()) {
    adjustNewConnectionIdForRoutine(new_cid.value(), original);
  }
  return (new_cid.has_value() && new_cid.value() == original) ? absl::nullopt : new_cid;
}

absl::optional<quic::QuicConnectionId>
EnvoyDeterministicConnectionIdGenerator::MaybeReplaceConnectionId(
    const quic::QuicConnectionId& original, const quic::ParsedQuicVersion& version) {
  auto new_cid = DeterministicConnectionIdGenerator::MaybeReplaceConnectionId(original, version);
  if (new_cid.has_value()) {
    adjustNewConnectionIdForRoutine(new_cid.value(), original);
  }
  return (new_cid.has_value() && new_cid.value() == original) ? absl::nullopt : new_cid;
}

QuicConnectionIdGeneratorPtr
EnvoyDeterministicConnectionIdGeneratorFactory::createQuicConnectionIdGenerator(uint32_t) {
  return std::make_unique<EnvoyDeterministicConnectionIdGenerator>(
      quic::kQuicDefaultConnectionIdLength);
}

Network::Socket::OptionConstSharedPtr
EnvoyDeterministicConnectionIdGeneratorFactory::createCompatibleLinuxBpfSocketOption(
    uint32_t concurrency) {
#if defined(SO_ATTACH_REUSEPORT_CBPF) && defined(__linux__)
  // This BPF filter reads the 1st word of QUIC connection id in the UDP payload and mods it by the
  // number of workers to get the socket index in the SO_REUSEPORT socket groups. QUIC packets
  // should be at least 9 bytes, with the 1st byte indicating one of the below QUIC packet headers:
  // 1) IETF QUIC long header: most significant bit is 1. The connection id starts from the 7th
  // byte.
  // 2) IETF QUIC short header: most significant bit is 0. The connection id starts from 2nd
  // byte.
  // 3) Google QUIC header: most significant bit is 0. The connection id starts from 2nd
  // byte.
  // Any packet that doesn't belong to any of the three packet header types are dispatched
  // based on 5-tuple source/destination addresses.
  // SPELLCHECKER(off)
  filter_ = {
      {0x80, 0, 0, 0000000000}, //                   ld len
      {0x35, 0, 9, 0x00000009}, //                   jlt #0x9, packet_too_short
      {0x30, 0, 0, 0000000000}, //                   ldb [0]
      {0x54, 0, 0, 0x00000080}, //                   and #0x80
      {0x15, 0, 2, 0000000000}, //                   jne #0, ietf_long_header
      {0x20, 0, 0, 0x00000001}, //                   ld [1]
      {0x05, 0, 0, 0x00000005}, //                   ja return
      {0x80, 0, 0, 0000000000}, // ietf_long_header: ld len
      {0x35, 0, 2, 0x0000000e}, //                   jlt #0xe, packet_too_short
      {0x20, 0, 0, 0x00000006}, //                   ld [6]
      {0x05, 0, 0, 0x00000001}, //                   ja return
      {0x20, 0, 0,              // packet_too_short: ld rxhash
       static_cast<uint32_t>(SKF_AD_OFF + SKF_AD_RXHASH)},
      {0x94, 0, 0, concurrency}, // return:         mod #socket_count
      {0x16, 0, 0, 0000000000},  //                 ret a
  };
  // SPELLCHECKER(on)

  // Note that this option refers to the BPF program data above, which must live until the
  // option is used. The program is kept as a member variable for this purpose.
  prog_.len = filter_.size();
  prog_.filter = filter_.data();
  return std::make_shared<Network::SocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_BOUND, ENVOY_ATTACH_REUSEPORT_CBPF,
      absl::string_view(reinterpret_cast<char*>(&prog_), sizeof(prog_)));
#else
  UNREFERENCED_PARAMETER(concurrency);
  PANIC("BPF filter is not supported in this platform.");
#endif
}

} // namespace Quic
} // namespace Envoy
