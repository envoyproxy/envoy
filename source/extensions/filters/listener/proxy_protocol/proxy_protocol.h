#pragma once

#include "envoy/event/file_event.h"
#include "envoy/extensions/filters/listener/proxy_protocol/v3/proxy_protocol.pb.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"
#include "source/extensions/filters/listener//proxy_protocol/proxy_protocol_header.h"

#include "absl/container/flat_hash_map.h"

using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_ADDR_LEN_UNIX;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_HEADER_LEN;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ProxyProtocol {

using KeyValuePair =
    envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol::KeyValuePair;

enum class ProxyProtocolVersion { NotFound = 0, V1 = 1, V2 = 2 };

enum class ReadOrParseState { Done, TryAgainLater, Error, Denied };

/**
 * Legacy stats that are under the root scope, not under the filter's scope.
 * Kept for backwards compatibility.
 * @deprecated Use GeneralProxyProtocolStats instead.
 * @see stats_macros.h
 */
// clang-format off
#define LEGACY_PROXY_PROTOCOL_STATS(COUNTER)                                          \
  COUNTER(downstream_cx_proxy_proto_error)
// clang-format on

struct LegacyProxyProtocolStats {
  LEGACY_PROXY_PROTOCOL_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Stats reported for the filter, rooted under the filter's scope.
 * @see stats_macros.h
 */
// clang-format off
#define GENERAL_PROXY_PROTOCOL_STATS(COUNTER)                                         \
  COUNTER(not_found_allowed)                                                          \
  COUNTER(not_found_disallowed)
// clang-format on

struct GeneralProxyProtocolStats {
  GENERAL_PROXY_PROTOCOL_STATS(GENERATE_COUNTER_STRUCT)

  /**
   * Increment the stats for the given filter decision.
   */
  void increment(ReadOrParseState decision);
};

/**
 * Stats reported for each version of the proxy protocol.
 * @see stats_macros.h
 */
// clang-format off
#define VERSIONED_PROXY_PROTOCOL_STATS(COUNTER)  \
  COUNTER(found)                                 \
  COUNTER(disallowed)                            \
  COUNTER(error)
// clang-format on

struct VersionedProxyProtocolStats {
  VERSIONED_PROXY_PROTOCOL_STATS(GENERATE_COUNTER_STRUCT)

  /**
   * Increment the stats for the given filter decision.
   */
  void increment(ReadOrParseState decision);
};

/**
 * Definition of all stats for the proxy protocol. @see stats_macros.h
 */
struct ProxyProtocolStats {
  LegacyProxyProtocolStats legacy_;
  GeneralProxyProtocolStats general_;
  VersionedProxyProtocolStats v1_;
  VersionedProxyProtocolStats v2_;

  /**
   * Create an instance of the stats struct with all stats for the filter.
   *
   * For backwards compatibility, the legacy stats are rooted under their own scope.
   * The general and versioned stats are correctly rooted at this filter's own scope.
   */
  static ProxyProtocolStats create(Stats::Scope& scope, absl::string_view stat_prefix);
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
   * Return null if the type of TLV is not needed otherwise a pointer to the KeyValuePair for
   * emitting to dynamic metadata.
   */
  const KeyValuePair* isTlvTypeNeeded(uint8_t type) const;

  /**
   * Number of TLV types that need to be parsed and saved to dynamic metadata.
   */
  size_t numberOfNeededTlvTypes() const;

  /**
   * Return true if the type of TLV is needed for pass-through.
   */
  bool isPassThroughTlvTypeNeeded(uint8_t type) const;

  /**
   * Filter configuration that determines if we should pass-through requests without
   * proxy protocol. Should only be configured to true for trusted downstreams.
   */
  bool allowRequestsWithoutProxyProtocol() const;

  /**
   * Filter configuration that determines if a version is disallowed.
   */
  bool isVersionV1Allowed() const;
  bool isVersionV2Allowed() const;

private:
  absl::flat_hash_map<uint8_t, KeyValuePair> tlv_types_;
  const bool allow_requests_without_proxy_protocol_;
  const bool pass_all_tlvs_;
  absl::flat_hash_set<uint8_t> pass_through_tlvs_{};
  bool allow_v1_{true};
  bool allow_v2_{true};
};

using ConfigSharedPtr = std::shared_ptr<Config>;

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
  Network::FilterStatus onData(Network::ListenerFilterBuffer& buffer) override;
  size_t maxReadBytes() const override { return max_proxy_protocol_len_; }

private:
  static const size_t MAX_PROXY_PROTO_LEN_V2 =
      PROXY_PROTO_V2_HEADER_LEN + PROXY_PROTO_V2_ADDR_LEN_UNIX;
  static const size_t MAX_PROXY_PROTO_LEN_V1 = 108;

  void onRead();
  ReadOrParseState parseBuffer(Network::ListenerFilterBuffer& buffer);

  /**
   * Helper function that attempts to read the proxy header
   * (delimited by \r\n if V1 format, or with length if V2)
   * @return ReadOrParseState
   */
  ReadOrParseState readProxyHeader(Network::ListenerFilterBuffer& buffer);

  bool parseTlvs(const uint8_t* buf, size_t len);
  ReadOrParseState readExtensions(Network::ListenerFilterBuffer& buffer);

  /**
   * Given a char * & len, parse the header as per spec.
   * @return bool true if parsing succeeded, false if parsing failed.
   */
  bool parseV1Header(const char* buf, size_t len);
  bool parseV2Header(const char* buf);
  absl::optional<size_t> lenV2Address(const char* buf);

  Network::ListenerFilterCallbacks* cb_{};

  // The index in buf_ where the search for '\r\n' should continue from
  size_t search_index_{1};

  ProxyProtocolVersion header_version_{ProxyProtocolVersion::NotFound};

  ConfigSharedPtr config_;

  absl::optional<WireHeader> proxy_protocol_header_;
  size_t max_proxy_protocol_len_{MAX_PROXY_PROTO_LEN_V2};

  // Store the parsed proxy protocol TLVs.
  Network::ProxyProtocolTLVVector parsed_tlvs_;
};

} // namespace ProxyProtocol
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
