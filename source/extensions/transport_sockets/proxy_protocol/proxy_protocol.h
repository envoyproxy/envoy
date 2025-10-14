#pragma once

#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"
#include "envoy/server/factory_context.h"
#include "envoy/stats/stats.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/formatter/substitution_format_string.h"
#include "source/extensions/transport_sockets/common/passthrough.h"

#include "absl/status/status.h"

using envoy::config::core::v3::ProxyProtocolConfig;
using envoy::config::core::v3::ProxyProtocolConfig_Version;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace ProxyProtocol {

#define ALL_PROXY_PROTOCOL_TRANSPORT_SOCKET_STATS(COUNTER)                                         \
  /* Upstream events counter. */                                                                   \
  COUNTER(v2_tlvs_exceed_max_length)

/**
 * Wrapper struct for upstream ProxyProtocol stats. @see stats_macros.h
 */
struct UpstreamProxyProtocolStats {
  ALL_PROXY_PROTOCOL_TRANSPORT_SOCKET_STATS(GENERATE_COUNTER_STRUCT)
};

// Structure to hold TLV formatter information for dynamic TLVs.
struct TlvFormatter {
  uint8_t type;
  Formatter::FormatterPtr formatter;
};

class UpstreamProxyProtocolSocket : public TransportSockets::PassthroughSocket,
                                    public Logger::Loggable<Logger::Id::connection> {
public:
  UpstreamProxyProtocolSocket(Network::TransportSocketPtr&& transport_socket,
                              Network::TransportSocketOptionsConstSharedPtr options,
                              ProxyProtocolConfig_Version version,
                              const UpstreamProxyProtocolStats& stats, bool pass_all_tlvs,
                              absl::flat_hash_set<uint8_t> pass_through_tlvs,
                              std::vector<Envoy::Network::ProxyProtocolTLV> added_tlvs,
                              const std::vector<TlvFormatter>& dynamic_tlv_formatters);

  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  void onConnected() override;

private:
  void generateHeader();
  void generateHeaderV1();
  void generateHeaderV2();
  // Combine host-level and config-level TLVs, with fallback if metadata fails to unpack.
  // Host-level has precedence over config-level TLVs.
  // If we fail to parse host metadata, we still read config TLVs.
  std::vector<Envoy::Network::ProxyProtocolTLV> buildCustomTLVs() const;
  // Evaluate dynamic TLV formatters and combine with static TLVs.
  std::vector<Envoy::Network::ProxyProtocolTLV>
  evaluateDynamicTLVs(const std::vector<Envoy::Network::ProxyProtocolTLV>& static_tlvs) const;
  Network::IoResult writeHeader();

  Network::TransportSocketOptionsConstSharedPtr options_;
  Network::TransportSocketCallbacks* callbacks_{};
  Buffer::OwnedImpl header_buffer_{};
  ProxyProtocolConfig_Version version_{ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1};
  const UpstreamProxyProtocolStats& stats_;
  const bool pass_all_tlvs_;
  absl::flat_hash_set<uint8_t> pass_through_tlvs_{};
  std::vector<Envoy::Network::ProxyProtocolTLV> added_tlvs_{};
  const std::vector<TlvFormatter>& dynamic_tlv_formatters_;
};

class UpstreamProxyProtocolSocketFactory : public PassthroughFactory {
public:
  UpstreamProxyProtocolSocketFactory(
      Network::UpstreamTransportSocketFactoryPtr transport_socket_factory,
      ProxyProtocolConfig_Version version, Stats::Scope& scope, bool pass_all_tlvs,
      absl::flat_hash_set<uint8_t> pass_through_tlvs,
      std::vector<Envoy::Network::ProxyProtocolTLV> added_tlvs,
      std::vector<TlvFormatter> dynamic_tlv_formatters);

  // Network::UpstreamTransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr host) const override;
  void hashKey(std::vector<uint8_t>& key,
               Network::TransportSocketOptionsConstSharedPtr options) const override;

  static UpstreamProxyProtocolStats generateUpstreamProxyProtocolStats(Stats::Scope& stats_scope) {
    const char prefix[]{"upstream.proxyprotocol."};
    return {ALL_PROXY_PROTOCOL_TRANSPORT_SOCKET_STATS(POOL_COUNTER_PREFIX(stats_scope, prefix))};
  }

  // Parse TLVs and create formatters for dynamic TLVs without throwing.
  // Returns an InvalidArgument status on validation or parsing errors.
  static absl::Status parseTLVs(absl::Span<const envoy::config::core::v3::TlvEntry* const> tlvs,
                                Server::Configuration::GenericFactoryContext& context,
                                std::vector<Envoy::Network::ProxyProtocolTLV>& static_tlvs,
                                std::vector<TlvFormatter>& dynamic_tlvs);

private:
  ProxyProtocolConfig_Version version_;
  UpstreamProxyProtocolStats stats_;
  bool pass_all_tlvs_;
  absl::flat_hash_set<uint8_t> pass_through_tlvs_;
  std::vector<Envoy::Network::ProxyProtocolTLV> added_tlvs_;
  std::vector<TlvFormatter> dynamic_tlv_formatters_;
};

} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
