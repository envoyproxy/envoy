#include "extensions/filters/listener/proxy_protocol/proxy_protocol.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/listen_socket.h"
#include "envoy/stats/scope.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "extensions/filters/listener/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ProxyProtocol {

Config::Config(
    Stats::Scope& scope,
    const envoy::extensions::filters::listener::proxy_protocol::v3::ProxyProtocol& proto_config)
    : stats_{ALL_PROXY_PROTOCOL_STATS(POOL_COUNTER(scope))} {
  for (const auto& rule : proto_config.rules()) {
    tlv_types_[0xFF & rule.tlv_type()] = rule.on_tlv_present();
  }
}

const KeyValuePair* Config::isTlvTypeNeeded(uint8_t type) const {
  auto tlv_type = tlv_types_.find(type);
  if (tlv_types_.end() != tlv_type) {
    return &tlv_type->second;
  }

  return nullptr;
}

size_t Config::numberOfNeededTlvTypes() const { return tlv_types_.size(); }

Network::FilterStatus Filter::onAccept(Network::ListenerFilterCallbacks& cb) {
  ENVOY_LOG(debug, "proxy_protocol: New connection accepted");
  Network::ConnectionSocket& socket = cb.socket();
  ASSERT(file_event_.get() == nullptr);
  file_event_ = cb.dispatcher().createFileEvent(
      socket.ioHandle().fd(),
      [this](uint32_t events) {
        ASSERT(events == Event::FileReadyType::Read);
        onRead();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
  cb_ = &cb;
  return Network::FilterStatus::StopIteration;
}

void Filter::onRead() {
  try {
    onReadWorker();
  } catch (const EnvoyException& ee) {
    config_->stats_.downstream_cx_proxy_proto_error_.inc();
    cb_->continueFilterChain(false);
  }
}

void Filter::onReadWorker() {
  Network::ConnectionSocket& socket = cb_->socket();

  if ((!proxy_protocol_header_.has_value() && !readProxyHeader(socket.ioHandle().fd())) ||
      (proxy_protocol_header_.has_value() && !readExtensions(socket.ioHandle().fd()))) {
    // We return if a) we do not yet have the header, or b) we have the header but not yet all
    // the extension data. In both cases we'll be called again when the socket is ready to read
    // and pick up where we left off.
    return;
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
      throw EnvoyException("failed to read proxy protocol");
    }
    // Check that both addresses are valid unicast addresses, as required for TCP
    if (!proxy_protocol_header_.value().remote_address_->ip()->isUnicastAddress() ||
        !proxy_protocol_header_.value().local_address_->ip()->isUnicastAddress()) {
      throw EnvoyException("failed to read proxy protocol");
    }

    // Only set the local address if it really changed, and mark it as address being restored.
    if (*proxy_protocol_header_.value().local_address_ != *socket.localAddress()) {
      socket.restoreLocalAddress(proxy_protocol_header_.value().local_address_);
    }
    socket.setRemoteAddress(proxy_protocol_header_.value().remote_address_);
  }

  // Release the file event so that we do not interfere with the connection read events.
  file_event_.reset();
  cb_->continueFilterChain(true);
}

size_t Filter::lenV2Address(char* buf) {
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
    throw EnvoyException("Unsupported V2 proxy protocol address family");
  }
  return len;
}

void Filter::parseV2Header(char* buf) {
  const int ver_cmd = buf[PROXY_PROTO_V2_SIGNATURE_LEN];
  uint8_t upper_byte = buf[PROXY_PROTO_V2_HEADER_LEN - 2];
  uint8_t lower_byte = buf[PROXY_PROTO_V2_HEADER_LEN - 1];
  size_t hdr_addr_len = (upper_byte << 8) + lower_byte;

  if ((ver_cmd & 0xf) == PROXY_PROTO_V2_LOCAL) {
    // This is locally-initiated, e.g. health-check, and should not override remote address
    proxy_protocol_header_.emplace(WireHeader{hdr_addr_len});
    return;
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
        pp_ipv4_addr* v4;
        v4 = reinterpret_cast<pp_ipv4_addr*>(&buf[PROXY_PROTO_V2_HEADER_LEN]);
        sockaddr_in ra4, la4;
        memset(&ra4, 0, sizeof(ra4));
        memset(&la4, 0, sizeof(la4));
        ra4.sin_family = AF_INET;
        ra4.sin_port = v4->src_port;
        ra4.sin_addr.s_addr = v4->src_addr;

        la4.sin_family = AF_INET;
        la4.sin_port = v4->dst_port;
        la4.sin_addr.s_addr = v4->dst_addr;
        proxy_protocol_header_.emplace(
            WireHeader{hdr_addr_len - PROXY_PROTO_V2_ADDR_LEN_INET, Network::Address::IpVersion::v4,
                       std::make_shared<Network::Address::Ipv4Instance>(&ra4),
                       std::make_shared<Network::Address::Ipv4Instance>(&la4)});
        return;
      } else if (((proto_family & 0xf0) >> 4) == PROXY_PROTO_V2_AF_INET6) {
        PACKED_STRUCT(struct pp_ipv6_addr {
          uint8_t src_addr[16];
          uint8_t dst_addr[16];
          uint16_t src_port;
          uint16_t dst_port;
        });
        pp_ipv6_addr* v6;
        v6 = reinterpret_cast<pp_ipv6_addr*>(&buf[PROXY_PROTO_V2_HEADER_LEN]);
        sockaddr_in6 ra6, la6;
        memset(&ra6, 0, sizeof(ra6));
        memset(&la6, 0, sizeof(la6));
        ra6.sin6_family = AF_INET6;
        ra6.sin6_port = v6->src_port;
        memcpy(ra6.sin6_addr.s6_addr, v6->src_addr, sizeof(ra6.sin6_addr.s6_addr));

        la6.sin6_family = AF_INET6;
        la6.sin6_port = v6->dst_port;
        memcpy(la6.sin6_addr.s6_addr, v6->dst_addr, sizeof(la6.sin6_addr.s6_addr));

        proxy_protocol_header_.emplace(WireHeader{
            hdr_addr_len - PROXY_PROTO_V2_ADDR_LEN_INET6, Network::Address::IpVersion::v6,
            std::make_shared<Network::Address::Ipv6Instance>(ra6),
            std::make_shared<Network::Address::Ipv6Instance>(la6)});
        return;
      }
    }
  }
  throw EnvoyException("Unsupported command or address family or transport");
}

void Filter::parseV1Header(char* buf, size_t len) {
  std::string proxy_line;
  proxy_line.assign(buf, len);
  const auto trimmed_proxy_line = StringUtil::rtrim(proxy_line);

  // Parse proxy protocol line with format: PROXY TCP4/TCP6/UNKNOWN SOURCE_ADDRESS
  // DESTINATION_ADDRESS SOURCE_PORT DESTINATION_PORT.
  const auto line_parts = StringUtil::splitToken(trimmed_proxy_line, " ", true);
  if (line_parts.size() < 2 || line_parts[0] != "PROXY") {
    throw EnvoyException("failed to read proxy protocol");
  }

  // If the line starts with UNKNOWN we know it's a proxy protocol line, so we can remove it from
  // the socket and continue. According to spec "real connection's parameters" should be used, so
  // we should NOT restore the addresses in this case.
  if (line_parts[1] != "UNKNOWN") {
    // If protocol not UNKNOWN, src and dst addresses have to be present.
    if (line_parts.size() != 6) {
      throw EnvoyException("failed to read proxy protocol");
    }

    // TODO(gsagula): parseInternetAddressAndPort() could be modified to take two string_view
    // arguments, so we can eliminate allocation here.
    if (line_parts[1] == "TCP4") {
      proxy_protocol_header_.emplace(
          WireHeader{0, Network::Address::IpVersion::v4,
                     Network::Utility::parseInternetAddressAndPort(
                         std::string{line_parts[2]} + ":" + std::string{line_parts[4]}),
                     Network::Utility::parseInternetAddressAndPort(
                         std::string{line_parts[3]} + ":" + std::string{line_parts[5]})});
    } else if (line_parts[1] == "TCP6") {
      proxy_protocol_header_.emplace(
          WireHeader{0, Network::Address::IpVersion::v6,
                     Network::Utility::parseInternetAddressAndPort(
                         "[" + std::string{line_parts[2]} + "]:" + std::string{line_parts[4]}),
                     Network::Utility::parseInternetAddressAndPort(
                         "[" + std::string{line_parts[3]} + "]:" + std::string{line_parts[5]})});
    } else {
      throw EnvoyException("failed to read proxy protocol");
    }
  }
}

bool Filter::parseExtensions(os_fd_t fd, uint8_t* buf, size_t buf_size, size_t* buf_off) {
  // If we ever implement extensions elsewhere, be sure to
  // continue to skip and ignore those for LOCAL.
  while (proxy_protocol_header_.value().extensions_length_) {
    int bytes_avail;
    auto& os_syscalls = Api::OsSysCallsSingleton::get();
    if (os_syscalls.ioctl(fd, FIONREAD, &bytes_avail).rc_ < 0) {
      throw EnvoyException("failed to read proxy protocol (no bytes avail)");
    }
    if (bytes_avail == 0) {
      return false;
    }
    bytes_avail = std::min(size_t(bytes_avail), buf_size);
    bytes_avail = std::min(size_t(bytes_avail), proxy_protocol_header_.value().extensions_length_);
    buf += (nullptr != buf_off) ? *buf_off : 0;
    const Api::SysCallSizeResult recv_result = os_syscalls.recv(fd, buf, bytes_avail, 0);
    if (recv_result.rc_ != bytes_avail) {
      throw EnvoyException("failed to read proxy protocol extension");
    }
    proxy_protocol_header_.value().extensions_length_ -= recv_result.rc_;

    if (nullptr != buf_off) {
      *buf_off += recv_result.rc_;
    }
  }

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
void Filter::parseTlvs(const std::vector<uint8_t>& tlvs) {
  size_t idx{0};
  while (idx < tlvs.size()) {
    const uint8_t tlv_type = tlvs[idx];
    idx++;

    if ((idx + 1) >= tlvs.size()) {
      throw EnvoyException(
          fmt::format("failed to read proxy protocol extension. No bytes for TLV length. "
                      "Extension length is {}, current index is {}, current type is {}.",
                      tlvs.size(), idx, tlv_type));
    }

    const uint8_t tlv_length_upper = tlvs[idx];
    const uint8_t tlv_length_lower = tlvs[idx + 1];
    const size_t tlv_value_length = (tlv_length_upper << 8) + tlv_length_lower;
    idx += 2;

    // Get the value.
    if ((idx + tlv_value_length - 1) >= tlvs.size()) {
      throw EnvoyException(
          fmt::format("failed to read proxy protocol extension. No bytes for TLV value. "
                      "Extension length is {}, current index is {}, current type is {}, current "
                      "value length is {}.",
                      tlvs.size(), idx, tlv_type, tlv_length_upper));
    }

    // Only save to dynamic metadata if this type of TLV is needed.
    auto key_value_pair = config_->isTlvTypeNeeded(tlv_type);
    if (nullptr != key_value_pair) {
      ProtobufWkt::Value metadata_value;
      metadata_value.set_string_value(reinterpret_cast<char const*>(tlvs.data() + idx),
                                      tlv_value_length);

      std::string metadata_key = key_value_pair->metadata_namespace().empty()
                                     ? ListenerFilterNames::get().ProxyProtocol
                                     : key_value_pair->metadata_namespace();

      ProtobufWkt::Struct metadata(
          (*cb_->dynamicMetadata().mutable_filter_metadata())[metadata_key]);
      metadata.mutable_fields()->insert({key_value_pair->key(), metadata_value});
      cb_->setDynamicMetadata(metadata_key, metadata);
    } else {
      ENVOY_LOG(trace, "proxy_protocol: Skip TLV of type {} since it's not needed", tlv_type);
    }

    idx += tlv_value_length;
    ASSERT(idx <= tlvs.size());
  }
}

bool Filter::readExtensions(os_fd_t fd) {
  // Parse and discard the extensions if this is a local command or there's no TLV needs to be saved
  // to metadata.
  if (proxy_protocol_header_.value().local_command_ || 0 == config_->numberOfNeededTlvTypes()) {
    // buf_ is no longer in use so we re-use it to read/discard.
    return parseExtensions(fd, reinterpret_cast<uint8_t*>(buf_), sizeof(buf_), nullptr);
  }

  // Initialize the buf_tlv_ only when we need to read the TLVs.
  if (buf_tlv_.empty()) {
    buf_tlv_.resize(proxy_protocol_header_.value().extensions_length_);
  }

  // Parse until we have all the TLVs in buf_tlv.
  if (!parseExtensions(fd, buf_tlv_.data(), buf_tlv_.size(), &buf_tlv_off_)) {
    return false;
  }

  parseTlvs(buf_tlv_);

  return true;
}

bool Filter::readProxyHeader(os_fd_t fd) {
  while (buf_off_ < MAX_PROXY_PROTO_LEN_V2) {
    int bytes_avail;
    auto& os_syscalls = Api::OsSysCallsSingleton::get();

    if (os_syscalls.ioctl(fd, FIONREAD, &bytes_avail).rc_ < 0) {
      throw EnvoyException("failed to read proxy protocol (no bytes avail)");
    }

    if (bytes_avail == 0) {
      return false;
    }

    bytes_avail = std::min(size_t(bytes_avail), MAX_PROXY_PROTO_LEN_V2 - buf_off_);

    const Api::SysCallSizeResult result =
        os_syscalls.recv(fd, buf_ + buf_off_, bytes_avail, MSG_PEEK);
    ssize_t nread = result.rc_;

    if (nread < 1) {
      throw EnvoyException("failed to read proxy protocol (no bytes read)");
    }

    if (buf_off_ + nread >= PROXY_PROTO_V2_HEADER_LEN) {
      const char* sig = PROXY_PROTO_V2_SIGNATURE;
      if (!memcmp(buf_, sig, PROXY_PROTO_V2_SIGNATURE_LEN)) {
        header_version_ = V2;
      } else if (memcmp(buf_, PROXY_PROTO_V1_SIGNATURE, PROXY_PROTO_V1_SIGNATURE_LEN)) {
        // It is not v2, and can't be v1, so no sense hanging around: it is invalid
        throw EnvoyException("failed to read proxy protocol (exceed max v1 header len)");
      }
    }

    if (header_version_ == V2) {
      const int ver_cmd = buf_[PROXY_PROTO_V2_SIGNATURE_LEN];
      if (((ver_cmd & 0xf0) >> 4) != PROXY_PROTO_V2_VERSION) {
        throw EnvoyException("Unsupported V2 proxy protocol version");
      }
      if (buf_off_ < PROXY_PROTO_V2_HEADER_LEN) {
        ssize_t exp = PROXY_PROTO_V2_HEADER_LEN - buf_off_;
        const Api::SysCallSizeResult read_result = os_syscalls.recv(fd, buf_ + buf_off_, exp, 0);
        if (read_result.rc_ != exp) {
          throw EnvoyException("failed to read proxy protocol (remote closed)");
        }
        buf_off_ += read_result.rc_;
        nread -= read_result.rc_;
      }
      ssize_t addr_len = lenV2Address(buf_);
      uint8_t upper_byte = buf_[PROXY_PROTO_V2_HEADER_LEN - 2];
      uint8_t lower_byte = buf_[PROXY_PROTO_V2_HEADER_LEN - 1];
      ssize_t hdr_addr_len = (upper_byte << 8) + lower_byte;
      if (hdr_addr_len < addr_len) {
        throw EnvoyException("failed to read proxy protocol (insufficient data)");
      }
      if (ssize_t(buf_off_) + nread >= PROXY_PROTO_V2_HEADER_LEN + addr_len) {
        ssize_t missing = (PROXY_PROTO_V2_HEADER_LEN + addr_len) - buf_off_;
        const Api::SysCallSizeResult read_result =
            os_syscalls.recv(fd, buf_ + buf_off_, missing, 0);
        if (read_result.rc_ != missing) {
          throw EnvoyException("failed to read proxy protocol (remote closed)");
        }
        buf_off_ += read_result.rc_;
        parseV2Header(buf_);
        // The TLV remain, they are read/discard in parseExtensions() which is called from the
        // parent (if needed).
        return true;
      } else {
        const Api::SysCallSizeResult result = os_syscalls.recv(fd, buf_ + buf_off_, nread, 0);
        nread = result.rc_;
        if (nread < 0) {
          throw EnvoyException("failed to read proxy protocol (remote closed)");
        }
        buf_off_ += nread;
      }
    } else {
      // continue searching buf_ from where we left off
      for (; search_index_ < buf_off_ + nread; search_index_++) {
        if (buf_[search_index_] == '\n' && buf_[search_index_ - 1] == '\r') {
          if (search_index_ == 1) {
            // This could be the binary protocol. It cannot be the ascii protocol
            header_version_ = InProgress;
          } else {
            header_version_ = V1;
            search_index_++;
          }
          break;
        }
      }

      // If we bailed on the first char, we might be v2, but are for sure not v1. Thus we
      // can read up to min(PROXY_PROTO_V2_HEADER_LEN, bytes_avail). If we bailed after first
      // char, but before we hit \r\n, read up to search_index_. We're asking only for
      // bytes we've already seen so there should be no block or fail
      size_t ntoread;
      if (header_version_ == InProgress) {
        ntoread = bytes_avail;
      } else {
        ntoread = search_index_ - buf_off_;
      }

      const Api::SysCallSizeResult result = os_syscalls.recv(fd, buf_ + buf_off_, ntoread, 0);
      nread = result.rc_;
      ASSERT(size_t(nread) == ntoread);

      buf_off_ += nread;

      if (header_version_ == V1) {
        parseV1Header(buf_, buf_off_);
        return true;
      }
    }
  }

  throw EnvoyException("failed to read proxy protocol (exceed max v2 header len)");
}

} // namespace ProxyProtocol
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
