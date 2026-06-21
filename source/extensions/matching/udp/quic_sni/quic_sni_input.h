#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/network/filter.h"

#include "source/common/buffer/buffer_impl.h"

#include "quiche/quic/core/quic_framer.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/core/tls_chlo_extractor.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Udp {
namespace QuicSni {

/**
 * Matching input that extracts the SNI (Server Name Indication) from a QUIC
 * Initial packet. Implements the Matcher::DataInput interface for UdpMatchingData.
 *
 * On a valid QUIC v1/v2 Initial containing a ClientHello with an SNI extension,
 * returns the domain name as a string. On any failure (not QUIC, unsupported
 * version, decryption failure, no SNI), returns monostate so the matcher moves
 * to the next rule.
 */
class QuicSniInput : public ::Envoy::Matcher::DataInput<Network::UdpMatchingData> {
public:
  ::Envoy::Matcher::DataInputGetResult get(const Network::UdpMatchingData& data) const override {
    const auto& buffer = data.data();
    if (buffer.length() == 0) {
      return {::Envoy::Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
              absl::monostate()};
    }

    // Get a raw view of the first contiguous slice (QUIC Initial is always in
    // the first datagram, padded to >= 1200 bytes by RFC 9000).
    Buffer::RawSlice slice = buffer.frontSlice();
    const quic::QuicReceivedPacket packet(reinterpret_cast<const char*>(slice.mem_), slice.len_,
                                          quic::QuicTime::Zero(),
                                          /*owns_buffer=*/false);

    // Parse the public header to determine packet type and version.
    quic::PacketHeaderFormat format;
    quic::QuicLongHeaderType long_packet_type;
    bool version_present = false;
    bool has_length_prefix = false;
    quic::QuicVersionLabel version_label;
    quic::ParsedQuicVersion parsed_version = quic::ParsedQuicVersion::Unsupported();
    quic::QuicConnectionId destination_connection_id;
    quic::QuicConnectionId source_connection_id;
    std::optional<absl::string_view> retry_token;
    std::string detailed_error;

    const quic::QuicErrorCode header_error = quic::QuicFramer::ParsePublicHeaderDispatcher(
        packet, /*expected_destination_connection_id_length=*/0, &format, &long_packet_type,
        &version_present, &has_length_prefix, &version_label, &parsed_version,
        &destination_connection_id, &source_connection_id, &retry_token, &detailed_error);

    // Not a QUIC Initial packet — exit fast.
    if (header_error != quic::QUIC_NO_ERROR || !version_present ||
        format != quic::IETF_QUIC_LONG_HEADER_PACKET || long_packet_type != quic::INITIAL) {
      return {::Envoy::Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
              absl::monostate()};
    }

    // Unknown QUIC version — can't derive Initial keys.
    if (!parsed_version.IsKnown()) {
      return {::Envoy::Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
              absl::monostate()};
    }

    // Use QUICHE's TlsChloExtractor: it handles Initial key derivation (HKDF),
    // header protection removal, AES-128-GCM decryption, CRYPTO frame
    // reassembly, and ClientHello parsing — all in one IngestPacket() call.
    quic::TlsChloExtractor extractor;
    extractor.IngestPacket(parsed_version, packet);

    if (!extractor.HasParsedFullChlo()) {
      return {::Envoy::Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
              absl::monostate()};
    }

    const std::string sni = extractor.server_name();
    if (sni.empty()) {
      // Valid ClientHello but no SNI extension (e.g., ECH or IP-literal).
      return {::Envoy::Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
              absl::monostate()};
    }

    return {::Envoy::Matcher::DataInputGetResult::DataAvailability::AllDataAvailable, sni};
  }
};

} // namespace QuicSni
} // namespace Udp
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
