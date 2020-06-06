#pragma once

#include <set>

#include "envoy/event/file_event.h"
#include "envoy/extensions/filters/listener/proxy_protocol/v3/proxy_protocol.pb.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/logger.h"

#include "proxy_protocol_header.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ProxyProtocol {

/**
 * All stats for the proxy protocol. @see stats_macros.h
 */
// clang-format off
#define ALL_PROXY_PROTOCOL_STATS(COUNTER)                                                          \
  COUNTER(downstream_cx_proxy_proto_error)
// clang-format on

/**
 * Definition of all stats for the proxy protocol. @see stats_macros.h
 */
struct ProxyProtocolStats {
  ALL_PROXY_PROTOCOL_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Global configuration for Proxy Protocol listener filter.
 */
class Config : public Logger::Loggable<Logger::Id::filter> {
public:
  Config(
      Stats::Scope& scope,
      const envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol& proto_config);

  ProxyProtocolStats stats_;

  /**
   * Return true if the type of TLV needs to be parsed and saved to dynamic metadata
   */
  bool isTlvTypeNeeded(uint8_t type) const;

  /**
   * Number of TLV types that need to be parsed and saved to dynamic metadata
   */
  size_t numberOfNeededTlvTypes() const;

private:
  std::set<uint8_t> tlv_types_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

enum ProxyProtocolVersion { Unknown = -1, InProgress = -2, V1 = 1, V2 = 2 };

/**
 * Implementation the PROXY Protocol listener filter
 * (https://github.com/haproxy/haproxy/blob/master/doc/proxy-protocol.txt)
 *
 * This implementation supports Proxy Protocol v1 (TCP/UDP, v4/v6),
 * and Proxy Protocol v2 (TCP/UDP, v4/v6).
 *
 * Non INET (AF_UNIX) address family in v2 is not supported, will throw an error.
 * Extensions (TLV) in v2 are skipped over.
 */
class Filter : public Network::ListenerFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const ConfigSharedPtr& config) : config_(config) {}

  // Network::ListenerFilter
  Network::FilterStatus onAccept(Network::ListenerFilterCallbacks& cb) override;

private:
  static const size_t MAX_PROXY_PROTO_LEN_V2 =
      PROXY_PROTO_V2_HEADER_LEN + PROXY_PROTO_V2_ADDR_LEN_UNIX;
  static const size_t MAX_PROXY_PROTO_LEN_V1 = 108;

  void onRead();
  void onReadWorker();

  /**
   * Helper function that attempts to read the proxy header
   * (delimited by \r\n if V1 format, or with length if V2)
   * throws EnvoyException on any socket errors.
   * @return bool true valid header, false if more data is needed.
   */
  bool readProxyHeader(os_fd_t fd);

  /**
   * Parse (and discard unknown) header extensions (until hdr.extensions_length == 0)
   */
  bool parseExtensions(os_fd_t fd, uint8_t* buf, size_t buf_size, size_t* buf_off = nullptr);
  void parseTlvs(const std::vector<uint8_t>& tlvs);
  bool readExtensions(os_fd_t fd);

  /**
   * Given a char * & len, parse the header as per spec
   */
  void parseV1Header(char* buf, size_t len);
  void parseV2Header(char* buf);
  size_t lenV2Address(char* buf);

  Network::ListenerFilterCallbacks* cb_{};
  Event::FileEventPtr file_event_;

  // The offset in buf_ that has been fully read
  size_t buf_off_{};

  // The index in buf_ where the search for '\r\n' should continue from
  size_t search_index_{1};

  ProxyProtocolVersion header_version_{Unknown};

  // Stores the portion of the first line that has been read so far.
  char buf_[MAX_PROXY_PROTO_LEN_V2];

  /**
   * Store the extension TLVs if they need to be read.
   */
  std::vector<uint8_t> buf_tlv_;

  /**
   * Flag showing whether the buf_tlv_ has been initialized
   */
  bool buf_tlv_init_{false};

  /**
   * The index in buf_tlv_ that has been fully read.
   */
  size_t buf_tlv_off_{};

  ConfigSharedPtr config_;

  absl::optional<WireHeader> proxy_protocol_header_;
};

} // namespace ProxyProtocol
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
