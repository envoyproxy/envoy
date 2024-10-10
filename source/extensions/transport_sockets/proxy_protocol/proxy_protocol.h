#pragma once

#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/stats.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/transport_sockets/common/passthrough.h"

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

class UpstreamProxyProtocolSocket : public TransportSockets::PassthroughSocket,
                                    public Logger::Loggable<Logger::Id::connection> {
public:
  UpstreamProxyProtocolSocket(Network::TransportSocketPtr&& transport_socket,
                              Network::TransportSocketOptionsConstSharedPtr options,
                              ProxyProtocolConfig config, const UpstreamProxyProtocolStats& stats);

  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  void onConnected() override;

private:
  void generateHeader();
  void generateHeaderV1();
  void generateHeaderV2();
  Network::IoResult writeHeader();

  Network::TransportSocketOptionsConstSharedPtr options_;
  Network::TransportSocketCallbacks* callbacks_{};
  Buffer::OwnedImpl header_buffer_{};
  ProxyProtocolConfig_Version version_{ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1};
  const UpstreamProxyProtocolStats& stats_;
  const bool pass_all_tlvs_;
  absl::flat_hash_set<uint8_t> pass_through_tlvs_{};
};

class UpstreamProxyProtocolSocketFactory : public PassthroughFactory {
public:
  UpstreamProxyProtocolSocketFactory(
      Network::UpstreamTransportSocketFactoryPtr transport_socket_factory,
      ProxyProtocolConfig config, Stats::Scope& scope);

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

private:
  ProxyProtocolConfig config_;
  UpstreamProxyProtocolStats stats_;
};

} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
