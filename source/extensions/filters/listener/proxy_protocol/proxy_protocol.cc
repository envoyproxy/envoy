#include "source/extensions/filters/listener/proxy_protocol/proxy_protocol.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/data/core/v3/tlv_metadata.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hex.h"
#include "source/common/common/safe_memcpy.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/proxy_protocol_filter_state.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"

using envoy::config::core::v3::ProxyProtocolConfig;
using envoy::config::core::v3::ProxyProtocolPassThroughTLVs;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V1_SIGNATURE;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V1_SIGNATURE_LEN;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_ADDR_LEN_INET;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_ADDR_LEN_INET6;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_AF_INET;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_AF_INET6;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_HEADER_LEN;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_LOCAL;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_ONBEHALF_OF;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_SIGNATURE;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_SIGNATURE_LEN;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_TRANSPORT_DGRAM;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_TRANSPORT_STREAM;
using Envoy::Extensions::Common::ProxyProtocol::PROXY_PROTO_V2_VERSION;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ProxyProtocol {

constexpr absl::string_view kProxyProtoStatsPrefix = "proxy_proto.";
constexpr absl::string_view kVersionStatsPrefix = "versions.";

ProxyProtocolStats ProxyProtocolStats::create(Stats::Scope& scope, absl::string_view stat_prefix) {
  std::string filter_stat_prefix = std::string(kProxyProtoStatsPrefix);
  if (!stat_prefix.empty()) {
    filter_stat_prefix = absl::StrCat(kProxyProtoStatsPrefix, stat_prefix, ".");
  }

  return {
      /*legacy_=*/{LEGACY_PROXY_PROTOCOL_STATS(POOL_COUNTER(scope))},
      /*general_=*/
      {GENERAL_PROXY_PROTOCOL_STATS(POOL_COUNTER_PREFIX(scope, filter_stat_prefix))},
      /*v1_=*/
      {VERSIONED_PROXY_PROTOCOL_STATS(POOL_COUNTER_PREFIX(
          scope, absl::StrCat(filter_stat_prefix, kVersionStatsPrefix, "v1.")))},
      /*v2_=*/
      {VERSIONED_PROXY_PROTOCOL_STATS(POOL_COUNTER_PREFIX(
          scope, absl::StrCat(filter_stat_prefix, kVersionStatsPrefix, "v2.")))},
  };
}

void GeneralProxyProtocolStats::increment(ReadOrParseState decision) {
  switch (decision) {
  case ReadOrParseState::Done:
    not_found_allowed_.inc();
    break;
  case ReadOrParseState::TryAgainLater:
    break; // Do nothing.
  case ReadOrParseState::Error:
    not_found_disallowed_.inc();
    break;
  case ReadOrParseState::Denied:
    IS_ENVOY_BUG("ReadOrParseState can never be Denied when proxy protocol is not found");
    break;
  }
}

void VersionedProxyProtocolStats::increment(ReadOrParseState decision) {
  switch (decision) {
  case ReadOrParseState::Done:
    found_.inc();
    break;
  case ReadOrParseState::TryAgainLater:
    break; // Do nothing.
  case ReadOrParseState::Error:
    error_.inc();
    break;
  case ReadOrParseState::Denied:
    found_.inc();
    disallowed_.inc();
    break;
  }
}

Config::Config(
    Stats::Scope& scope,
    const envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol& proto_config)
    : stats_(ProxyProtocolStats::create(scope, proto_config.stat_prefix())),
      allow_requests_without_proxy_protocol_(proto_config.allow_requests_without_proxy_protocol()),
      pass_all_tlvs_(proto_config.has_pass_through_tlvs()
                         ? proto_config.pass_through_tlvs().match_type() ==
                               ProxyProtocolPassThroughTLVs::INCLUDE_ALL
                         : false) {
  for (const auto& rule : proto_config.rules()) {
    tlv_types_[0xFF & rule.tlv_type()] = rule.on_tlv_present();
  }

  if (proto_config.has_pass_through_tlvs() &&
      proto_config.pass_through_tlvs().match_type() == ProxyProtocolPassThroughTLVs::INCLUDE) {
    for (const auto& tlv_type : proto_config.pass_through_tlvs().tlv_type()) {
      pass_through_tlvs_.insert(0xFF & tlv_type);
    }
  }

  for (const auto& version : proto_config.disallowed_versions()) {
    switch (version) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case ProxyProtocolConfig::V1:
      allow_v1_ = false;
      break;
    case ProxyProtocolConfig::V2:
      allow_v2_ = false;
      break;
    }
  }

  // Remove this check if PROXY protocol v3 is ever introduced.
  if (!allow_v1_ && !allow_v2_) {
    throw ProtoValidationException(
        "Proxy Protocol filter is misconfigured: all proxy protocol versions are disallowed.");
  }
}

const KeyValuePair* Config::isTlvTypeNeeded(uint8_t type) const {
  auto tlv_type = tlv_types_.find(type);
  if (tlv_types_.end() != tlv_type) {
    return &tlv_type->second;
  }

  return nullptr;
}

bool Config::isPassThroughTlvTypeNeeded(uint8_t tlv_type) const {
  if (pass_all_tlvs_) {
    return true;
  }
  return pass_through_tlvs_.contains(tlv_type);
}

size_t Config::numberOfNeededTlvTypes() const { return tlv_types_.size(); }

bool Config::allowRequestsWithoutProxyProtocol() const {
  return allow_requests_without_proxy_protocol_;
}

bool Config::isVersionV1Allowed() const { return allow_v1_; }

bool Config::isVersionV2Allowed() const { return allow_v2_; }

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "proxy_protocol: New connection accepted");
  cb_ = &cb;
  // Waiting for data.
  return Network::FilterStatus::StopIteration;
}

Network::FilterStatus Filter::onData(Network::ListenerFilterBuffer& buffer) {
  const ReadOrParseState read_state = parseBuffer(buffer); // Implicitly updates header_version_

  switch (header_version_) {
  case ProxyProtocolVersion::V1:
    config_->stats_.v1_.increment(read_state);
    break;
  case ProxyProtocolVersion::V2:
    config_->stats_.v2_.increment(read_state);
    break;
  case ProxyProtocolVersion::NotFound:
    config_->stats_.general_.increment(read_state);
    break;
  }

  switch (read_state) {
  case ReadOrParseState::Denied:
    cb_->socket().ioHandle().close();
    return Network::FilterStatus::StopIteration;
  case ReadOrParseState::Error:
    config_->stats_.legacy_.downstream_cx_proxy_proto_error_
        .inc(); // Keep for backwards-compatibility
    cb_->socket().ioHandle().close();
    return Network::FilterStatus::StopIteration;
  case ReadOrParseState::TryAgainLater:
    return Network::FilterStatus::StopIteration;
  case ReadOrParseState::Done:
    return Network::FilterStatus::Continue;
  }
  return Network::FilterStatus::Continue;
}

ReadOrParseState Filter::parseBuffer(Network::ListenerFilterBuffer& buffer) {
  Network::ConnectionSocket& socket = cb_->socket();

  // We return if a) we do not yet have the header, b) we have the header but not yet all
  // the extension data.
  if (!proxy_protocol_header_.has_value()) {
    const ReadOrParseState read_header_state = readProxyHeader(buffer);
    if (read_header_state != ReadOrParseState::Done) {
      return read_header_state;
    }
    if (header_version_ == ProxyProtocolVersion::NotFound) {
      // Filter is skipped and request is allowed through.
      return ReadOrParseState::Done;
    }
  }

  // After parse the header, the extensions size is discovered. Then extend the buffer
  // size to receive the extensions.
  if (proxy_protocol_header_.value().wholeHeaderLength() > max_proxy_protocol_len_) {
    max_proxy_protocol_len_ = proxy_protocol_header_.value().wholeHeaderLength();
    // The expected header size is changed, waiting for more data.
    return ReadOrParseState::TryAgainLater;
  }

  if (proxy_protocol_header_.has_value()) {
    const ReadOrParseState read_ext_state = readExtensions(buffer);
    if (read_ext_state != ReadOrParseState::Done) {
      return read_ext_state;
    }
  }

  if (proxy_protocol_header_.has_value() &&
      !cb_->filterState().hasData<Network::ProxyProtocolFilterState>(
          Network::ProxyProtocolFilterState::key())) {
    auto buf = reinterpret_cast<const uint8_t*>(buffer.rawSlice().mem_);
    if (proxy_protocol_header_.value().local_command_) {
      ENVOY_LOG(trace, "Parsed proxy protocol header, cmd: LOCAL, length: {}, buffer: {}",
                proxy_protocol_header_.value().wholeHeaderLength(),
                Envoy::Hex::encode(buf, proxy_protocol_header_.value().wholeHeaderLength()));

      cb_->filterState().setData(
          Network::ProxyProtocolFilterState::key(),
          std::make_unique<Network::ProxyProtocolFilterState>(Network::ProxyProtocolData{
              socket.connectionInfoProvider().remoteAddress(),
              socket.connectionInfoProvider().localAddress(), parsed_tlvs_}),
          StreamInfo::FilterState::StateType::Mutable,
          StreamInfo::FilterState::LifeSpan::Connection);
    } else {
      ENVOY_LOG(
          trace,
          "Parsed proxy protocol header, cmd: PROXY, length: {}, buffer: {}, TLV length: {}, TLV "
          "buffer: {}",
          proxy_protocol_header_.value().wholeHeaderLength(),
          Envoy::Hex::encode(buf, proxy_protocol_header_.value().wholeHeaderLength()),
          proxy_protocol_header_.value().extensions_length_,
          Envoy::Hex::encode(buf + proxy_protocol_header_.value().headerLengthWithoutExtension(),
                             proxy_protocol_header_.value().extensions_length_));
      cb_->filterState().setData(
          Network::ProxyProtocolFilterState::key(),
          std::make_unique<Network::ProxyProtocolFilterState>(Network::ProxyProtocolData{
              proxy_protocol_header_.value().remote_address_,
              proxy_protocol_header_.value().local_address_, parsed_tlvs_}),
          StreamInfo::FilterState::StateType::Mutable,
          StreamInfo::FilterState::LifeSpan::Connection);
    }
  }

  if (proxy_protocol_header_.has_value() && !proxy_protocol_header_.value().local_command_) {
    // If this is a local_command, we are not to override address
    // Error check the source and destination fields. Most errors are caught by the address
    // parsing above, but a malformed IPv6 address may combine with a malformed port and parse as
    // an IPv6 address when parsing for an IPv4 address(for v1 mode). Remote address refers to the
    // source address.
    const auto remote_version = proxy_protocol_header_.value().remote_address_->ip()->version();
    const auto local_version = proxy_protocol_header_.value().local_address_->ip()->version();
    if (remote_version != proxy_protocol_header_.value().protocol_version_ ||
        local_version != proxy_protocol_header_.value().protocol_version_) {
      ENVOY_LOG(debug, "failed to read proxy protocol");
      return ReadOrParseState::Error;
    }
    // Check that both addresses are valid unicast addresses, as required for TCP
    if (!proxy_protocol_header_.value().remote_address_->ip()->isUnicastAddress() ||
        !proxy_protocol_header_.value().local_address_->ip()->isUnicastAddress()) {
      ENVOY_LOG(debug, "failed to read proxy protocol");
      return ReadOrParseState::Error;
    }

    // Only set the local address if it really changed, and mark it as address being restored.
    if (*proxy_protocol_header_.value().local_address_ !=
        *socket.connectionInfoProvider().localAddress()) {
      socket.connectionInfoProvider().restoreLocalAddress(
          proxy_protocol_header_.value().local_address_);
    }
    socket.connectionInfoProvider().setRemoteAddress(
        proxy_protocol_header_.value().remote_address_);
  }

  if (!buffer.drain(proxy_protocol_header_.value().wholeHeaderLength())) {
    return ReadOrParseState::Error;
  }
  return ReadOrParseState::Done;
}

absl::optional<size_t> Filter::lenV2Address(const char* buf) {
  const uint8_t proto_family = buf[PROXY_PROTO_V2_SIGNATURE_LEN + 1];
  const int ver_cmd = buf[PROXY_PROTO_V2_SIGNATURE_LEN];
  size_t len;

  if ((ver_cmd & 0xf) == PROXY_PROTO_V2_LOCAL) {
    // According to the spec there is no address encoded, len=0, and we must ignore
    return 0;
  }

  switch ((proto_family & 0xf0) >> 4) {
  case PROXY_PROTO_V2_AF_INET:
    len = PROXY_PROTO_V2_ADDR_LEN_INET;
    break;
  case PROXY_PROTO_V2_AF_INET6:
    len = PROXY_PROTO_V2_ADDR_LEN_INET6;
    break;
  default:
    ENVOY_LOG(debug, "Unsupported V2 proxy protocol address family");
    return absl::nullopt;
  }
  return len;
}

bool Filter::parseV2Header(const char* buf) {
  const int ver_cmd = buf[PROXY_PROTO_V2_SIGNATURE_LEN];
  uint8_t upper_byte = buf[PROXY_PROTO_V2_HEADER_LEN - 2];
  uint8_t lower_byte = buf[PROXY_PROTO_V2_HEADER_LEN - 1];
  size_t hdr_addr_len = (upper_byte << 8) + lower_byte;

  if ((ver_cmd & 0xf) == PROXY_PROTO_V2_LOCAL) {
    // This is locally-initiated, e.g. health-check, and should not override remote address.
    // According to the spec, this address length should be zero for local connection.
    proxy_protocol_header_.emplace(WireHeader{PROXY_PROTO_V2_HEADER_LEN, hdr_addr_len, 0, 0});
    return true;
  }

  // Only do connections on behalf of another user, not internally-driven health-checks. If
  // its not on behalf of someone, or its not AF_INET{6} / STREAM/DGRAM, ignore and
  // use the real-remote info
  if ((ver_cmd & 0xf) == PROXY_PROTO_V2_ONBEHALF_OF) {
    uint8_t proto_family = buf[PROXY_PROTO_V2_SIGNATURE_LEN + 1];
    if (((proto_family & 0x0f) == PROXY_PROTO_V2_TRANSPORT_STREAM) ||
        ((proto_family & 0x0f) == PROXY_PROTO_V2_TRANSPORT_DGRAM)) {
      if (((proto_family & 0xf0) >> 4) == PROXY_PROTO_V2_AF_INET) {
        PACKED_STRUCT(struct pp_ipv4_addr {
          uint32_t src_addr;
          uint32_t dst_addr;
          uint16_t src_port;
          uint16_t dst_port;
        });
        const pp_ipv4_addr* v4;
        v4 = reinterpret_cast<const pp_ipv4_addr*>(&buf[PROXY_PROTO_V2_HEADER_LEN]);
        sockaddr_in ra4, la4;
        memset(&ra4, 0, sizeof(ra4));
        memset(&la4, 0, sizeof(la4));
        ra4.sin_family = AF_INET;
        ra4.sin_port = v4->src_port;
        ra4.sin_addr.s_addr = v4->src_addr;

        la4.sin_family = AF_INET;
        la4.sin_port = v4->dst_port;
        la4.sin_addr.s_addr = v4->dst_addr;

        TRY_NEEDS_AUDIT_ADDRESS {
          // TODO(ggreenway): make this work without requiring operating system support for an
          // address family.
          proxy_protocol_header_.emplace(WireHeader{
              PROXY_PROTO_V2_HEADER_LEN, hdr_addr_len, PROXY_PROTO_V2_ADDR_LEN_INET,
              hdr_addr_len - PROXY_PROTO_V2_ADDR_LEN_INET, Network::Address::IpVersion::v4,
              std::make_shared<Network::Address::Ipv4Instance>(&ra4),
              std::make_shared<Network::Address::Ipv4Instance>(&la4)});
        }
        END_TRY CATCH(const EnvoyException& e, {
          ENVOY_LOG(debug, "Proxy protocol failure: {}", e.what());
          return false;
        });

        return true;
      } else if (((proto_family & 0xf0) >> 4) == PROXY_PROTO_V2_AF_INET6) {
        PACKED_STRUCT(struct pp_ipv6_addr {
          uint8_t src_addr[16];
          uint8_t dst_addr[16];
          uint16_t src_port;
          uint16_t dst_port;
        });
        const pp_ipv6_addr* v6;
        v6 = reinterpret_cast<const pp_ipv6_addr*>(&buf[PROXY_PROTO_V2_HEADER_LEN]);
        sockaddr_in6 ra6, la6;
        memset(&ra6, 0, sizeof(ra6));
        memset(&la6, 0, sizeof(la6));
        ra6.sin6_family = AF_INET6;
        ra6.sin6_port = v6->src_port;
        safeMemcpy(&(ra6.sin6_addr.s6_addr), &(v6->src_addr));

        la6.sin6_family = AF_INET6;
        la6.sin6_port = v6->dst_port;
        safeMemcpy(&(la6.sin6_addr.s6_addr), &(v6->dst_addr));

        TRY_NEEDS_AUDIT_ADDRESS {
          proxy_protocol_header_.emplace(WireHeader{
              PROXY_PROTO_V2_HEADER_LEN, hdr_addr_len, PROXY_PROTO_V2_ADDR_LEN_INET6,
              hdr_addr_len - PROXY_PROTO_V2_ADDR_LEN_INET6, Network::Address::IpVersion::v6,
              std::make_shared<Network::Address::Ipv6Instance>(ra6),
              std::make_shared<Network::Address::Ipv6Instance>(la6)});
        }
        END_TRY CATCH(const EnvoyException& e, {
          // TODO(ggreenway): make this work without requiring operating system support for an
          // address family.
          ENVOY_LOG(debug, "Proxy protocol failure: {}", e.what());
          return false;
        });
        return true;
      }
    }
  }
  ENVOY_LOG(debug, "Unsupported command or address family or transport");
  return false;
}

bool Filter::parseV1Header(const char* buf, size_t len) {
  std::string proxy_line;
  proxy_line.assign(buf, len);
  const auto trimmed_proxy_line = StringUtil::rtrim(proxy_line);

  // Parse proxy protocol line with format: PROXY TCP4/TCP6/UNKNOWN SOURCE_ADDRESS
  // DESTINATION_ADDRESS SOURCE_PORT DESTINATION_PORT.
  const auto line_parts = StringUtil::splitToken(trimmed_proxy_line, " ", true);
  if (line_parts.size() < 2 || line_parts[0] != "PROXY") {
    ENVOY_LOG(debug, "failed to read proxy protocol");
    return false;
  }

  // If the line starts with UNKNOWN we know it's a proxy protocol line, so we can remove it from
  // the socket and continue. According to spec "real connection's parameters" should be used, so
  // we should NOT restore the addresses in this case.
  if (line_parts[1] != "UNKNOWN") {
    // If protocol not UNKNOWN, src and dst addresses have to be present.
    if (line_parts.size() != 6) {
      ENVOY_LOG(debug, "failed to read proxy protocol");
      return false;
    }

    // TODO(gsagula): parseInternetAddressAndPortNoThrow() could be modified to take two string_view
    // arguments, so we can eliminate allocation here.
    if (line_parts[1] == "TCP4") {
      const Network::Address::InstanceConstSharedPtr remote_address =
          Network::Utility::parseInternetAddressAndPortNoThrow(std::string{line_parts[2]} + ":" +
                                                               std::string{line_parts[4]});
      const Network::Address::InstanceConstSharedPtr local_address =
          Network::Utility::parseInternetAddressAndPortNoThrow(std::string{line_parts[3]} + ":" +
                                                               std::string{line_parts[5]});

      if (remote_address == nullptr || local_address == nullptr) {
        return false;
      }
      proxy_protocol_header_.emplace(
          WireHeader{len, 0, 0, 0, Network::Address::IpVersion::v4, remote_address, local_address});
      return true;
    } else if (line_parts[1] == "TCP6") {
      const Network::Address::InstanceConstSharedPtr remote_address =
          Network::Utility::parseInternetAddressAndPortNoThrow("[" + std::string{line_parts[2]} +
                                                               "]:" + std::string{line_parts[4]});
      const Network::Address::InstanceConstSharedPtr local_address =
          Network::Utility::parseInternetAddressAndPortNoThrow("[" + std::string{line_parts[3]} +
                                                               "]:" + std::string{line_parts[5]});

      if (remote_address == nullptr || local_address == nullptr) {
        return false;
      }
      proxy_protocol_header_.emplace(
          WireHeader{len, 0, 0, 0, Network::Address::IpVersion::v6, remote_address, local_address});
      return true;
    } else {
      ENVOY_LOG(debug, "failed to read proxy protocol");
      return false;
    }
  }
  proxy_protocol_header_.emplace(WireHeader{len, 0, 0, 0});
  return true;
}

/**
 * @note  A TLV is arranged in the following format:
 *        struct pp2_tlv {
 *          uint8_t type;
 *          uint8_t length_hi;
 *          uint8_t length_lo;
 *          uint8_t value[0];
 *        };
 *        See https://www.haproxy.org/download/2.1/doc/proxy-protocol.txt for details
 */
bool Filter::parseTlvs(const uint8_t* buf, size_t len) {
  size_t idx{0};
  while (idx < len) {
    const uint8_t tlv_type = buf[idx];
    idx++;

    if ((idx + 1) >= len) {
      ENVOY_LOG(debug,
                fmt::format("failed to read proxy protocol extension. No bytes for TLV length. "
                            "Extension length is {}, current index is {}, current type is {}.",
                            len, idx, tlv_type));
      return false;
    }

    const uint8_t tlv_length_upper = buf[idx];
    const uint8_t tlv_length_lower = buf[idx + 1];
    const size_t tlv_value_length = (tlv_length_upper << 8) + tlv_length_lower;
    idx += 2;

    // Get the value.
    if ((idx + tlv_value_length - 1) >= len) {
      ENVOY_LOG(
          debug,
          fmt::format("failed to read proxy protocol extension. No bytes for TLV value. "
                      "Extension length is {}, current index is {}, current type is {}, current "
                      "value length is {}.",
                      len, idx, tlv_type, tlv_length_upper));
      return false;
    }

    // Only save to dynamic metadata if this type of TLV is needed.
    absl::string_view tlv_value(reinterpret_cast<char const*>(buf + idx), tlv_value_length);
    auto key_value_pair = config_->isTlvTypeNeeded(tlv_type);
    if (nullptr != key_value_pair) {
      std::string metadata_key = key_value_pair->metadata_namespace().empty()
                                     ? "envoy.filters.listener.proxy_protocol"
                                     : key_value_pair->metadata_namespace();
      if (Runtime::runtimeFeatureEnabled(
              "envoy.reloadable_features.use_typed_metadata_in_proxy_protocol_listener")) {
        auto& typed_filter_metadata = (*cb_->dynamicMetadata().mutable_typed_filter_metadata());

        const auto typed_proxy_filter_metadata = typed_filter_metadata.find(metadata_key);
        envoy::data::core::v3::TlvsMetadata tlvs_metadata;
        auto status = absl::OkStatus();
        if (typed_proxy_filter_metadata != typed_filter_metadata.end()) {
          status = MessageUtil::unpackTo(typed_proxy_filter_metadata->second, tlvs_metadata);
        }
        if (!status.ok()) {
          ENVOY_LOG_PERIODIC(warn, std::chrono::seconds(1),
                             "proxy_protocol: Failed to unpack typed metadata for TLV type ",
                             tlv_type);
        } else {
          Protobuf::BytesValue tlv_byte_value;
          tlv_byte_value.set_value(tlv_value.data(), tlv_value.size());
          tlvs_metadata.mutable_typed_metadata()->insert(
              {key_value_pair->key(), tlv_byte_value.value()});
          ProtobufWkt::Any typed_metadata;
          typed_metadata.PackFrom(tlvs_metadata);
          cb_->setDynamicTypedMetadata(metadata_key, typed_metadata);
        }
      }
      // Always populate untyped metadata for backwards compatibility.
      ProtobufWkt::Value metadata_value;
      // Sanitize any non utf8 characters.
      auto sanitised_tlv_value = MessageUtil::sanitizeUtf8String(tlv_value);
      metadata_value.set_string_value(sanitised_tlv_value.data(), sanitised_tlv_value.size());
      ProtobufWkt::Struct metadata(
          (*cb_->dynamicMetadata().mutable_filter_metadata())[metadata_key]);
      metadata.mutable_fields()->insert({key_value_pair->key(), metadata_value});
      cb_->setDynamicMetadata(metadata_key, metadata);
    } else {
      ENVOY_LOG(trace,
                "proxy_protocol: Skip TLV of type {} since it's not needed for dynamic metadata",
                tlv_type);
    }

    // Save TLVs to the filter state.
    if (config_->isPassThroughTlvTypeNeeded(tlv_type)) {
      ENVOY_LOG(trace, "proxy_protocol: Storing parsed TLV of type {} to filter state.", tlv_type);
      parsed_tlvs_.push_back({tlv_type, {tlv_value.begin(), tlv_value.end()}});
    }

    idx += tlv_value_length;
    ASSERT(idx <= len);
  }
  return true;
}

ReadOrParseState Filter::readExtensions(Network::ListenerFilterBuffer& buffer) {
  auto raw_slice = buffer.rawSlice();
  // waiting for more data if there is no enough data for extensions.
  if (raw_slice.len_ < (proxy_protocol_header_.value().wholeHeaderLength())) {
    return ReadOrParseState::TryAgainLater;
  }

  if (proxy_protocol_header_.value().local_command_) {
    // Ignores the extensions if this is a local command.
    // Those will drained from the buffer in the end.
    return ReadOrParseState::Done;
  }

  const uint8_t* buf = static_cast<const uint8_t*>(raw_slice.mem_) +
                       proxy_protocol_header_.value().headerLengthWithoutExtension();
  if (!parseTlvs(buf, proxy_protocol_header_.value().extensions_length_)) {
    return ReadOrParseState::Error;
  }

  return ReadOrParseState::Done;
}

ReadOrParseState Filter::readProxyHeader(Network::ListenerFilterBuffer& buffer) {
  auto raw_slice = buffer.rawSlice();
  const char* buf = static_cast<const char*>(raw_slice.mem_);

  if (config_->allowRequestsWithoutProxyProtocol()) {
    auto matchv2 = config_->isVersionV2Allowed() &&
                   !memcmp(buf, PROXY_PROTO_V2_SIGNATURE,
                           std::min<size_t>(PROXY_PROTO_V2_SIGNATURE_LEN, raw_slice.len_));
    auto matchv1 = config_->isVersionV1Allowed() &&
                   !memcmp(buf, PROXY_PROTO_V1_SIGNATURE,
                           std::min<size_t>(PROXY_PROTO_V1_SIGNATURE_LEN, raw_slice.len_));
    if (!matchv2 && !matchv1) {
      // The bytes we have seen so far do not match v1 or v2 proxy protocol, so we can safely
      // short-circuit
      ENVOY_LOG(trace, "request does not use v1 or v2 proxy protocol, forwarding as is");
      return ReadOrParseState::Done;
    }
  }

  if (raw_slice.len_ >= PROXY_PROTO_V2_HEADER_LEN) {
    const char* sig = PROXY_PROTO_V2_SIGNATURE;
    if (!memcmp(buf, sig, PROXY_PROTO_V2_SIGNATURE_LEN)) {
      header_version_ = ProxyProtocolVersion::V2;
    } else if (memcmp(buf, PROXY_PROTO_V1_SIGNATURE, PROXY_PROTO_V1_SIGNATURE_LEN)) {
      // It is not v2, and can't be v1, so no sense hanging around: it is invalid
      ENVOY_LOG(debug, "failed to read proxy protocol (exceed max v1 header len)");
      return ReadOrParseState::Error;
    }
  }

  if (header_version_ == ProxyProtocolVersion::V2) {
    if (!config_->isVersionV2Allowed()) {
      ENVOY_LOG(trace, "Filter is not configured to allow v2 proxy protocol requests");
      return ReadOrParseState::Denied;
    }
    const int ver_cmd = buf[PROXY_PROTO_V2_SIGNATURE_LEN];
    if (((ver_cmd & 0xf0) >> 4) != PROXY_PROTO_V2_VERSION) {
      ENVOY_LOG(debug, "Unsupported V2 proxy protocol version");
      return ReadOrParseState::Error;
    }
    absl::optional<ssize_t> addr_len_opt = lenV2Address(buf);
    if (!addr_len_opt.has_value()) {
      return ReadOrParseState::Error;
    }
    ssize_t addr_len = addr_len_opt.value();
    uint8_t upper_byte = buf[PROXY_PROTO_V2_HEADER_LEN - 2];
    uint8_t lower_byte = buf[PROXY_PROTO_V2_HEADER_LEN - 1];
    ssize_t hdr_addr_len = (upper_byte << 8) + lower_byte;
    if (hdr_addr_len < addr_len) {
      ENVOY_LOG(debug,
                "incorrect address length, address length = {}, the expected address length = {}",
                hdr_addr_len, addr_len);
      return ReadOrParseState::Error;
    }
    // waiting for more data if there is no enough data for address.
    if (raw_slice.len_ >= static_cast<size_t>(PROXY_PROTO_V2_HEADER_LEN + addr_len)) {
      // The TLV remain, they are parsed in `parseTlvs()` which is called from the
      // parent (if needed).
      if (parseV2Header(buf)) {
        return ReadOrParseState::Done;
      } else {
        return ReadOrParseState::Error;
      }
    }
  } else {
    // continue searching buffer from where we left off
    for (; search_index_ < raw_slice.len_; search_index_++) {
      if (buf[search_index_] == '\n' && buf[search_index_ - 1] == '\r') {
        if (search_index_ == 1) {
          // There is not enough data to determine if it contains the v2 protocol signature, so wait
          // for more data.
          break;
        } else {
          header_version_ = ProxyProtocolVersion::V1;
          search_index_++;
        }
        break;
      }
    }

    if (search_index_ > MAX_PROXY_PROTO_LEN_V1) {
      return ReadOrParseState::Error;
    }

    if (header_version_ == ProxyProtocolVersion::V1) {
      if (!config_->isVersionV1Allowed()) {
        ENVOY_LOG(trace, "Filter is not configured to allow v1 proxy protocol requests");
        return ReadOrParseState::Denied;
      }
      if (parseV1Header(buf, search_index_)) {
        return ReadOrParseState::Done;
      } else {
        return ReadOrParseState::Error;
      }
    }
  }

  return ReadOrParseState::TryAgainLater;
}

} // namespace ProxyProtocol
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
