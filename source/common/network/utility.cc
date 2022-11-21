#include "source/common/network/utility.h"

#include <cstdint>
#include <list>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/connection.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Network {

Address::InstanceConstSharedPtr instanceOrNull(StatusOr<Address::InstanceConstSharedPtr> address) {
  if (address.ok()) {
    return *address;
  }
  return nullptr;
}

Address::InstanceConstSharedPtr Utility::resolveUrl(const std::string& url) {
  if (urlIsTcpScheme(url)) {
    return parseInternetAddressAndPort(url.substr(TCP_SCHEME.size()));
  } else if (urlIsUdpScheme(url)) {
    return parseInternetAddressAndPort(url.substr(UDP_SCHEME.size()));
  } else if (urlIsUnixScheme(url)) {
    return std::make_shared<Address::PipeInstance>(url.substr(UNIX_SCHEME.size()));
  } else {
    throw EnvoyException(absl::StrCat("unknown protocol scheme: ", url));
  }
}

StatusOr<Socket::Type> Utility::socketTypeFromUrl(const std::string& url) {
  if (urlIsTcpScheme(url)) {
    return Socket::Type::Stream;
  } else if (urlIsUdpScheme(url)) {
    return Socket::Type::Datagram;
  } else if (urlIsUnixScheme(url)) {
    return Socket::Type::Stream;
  } else {
    return absl::InvalidArgumentError(absl::StrCat("unknown protocol scheme: ", url));
  }
}

bool Utility::urlIsTcpScheme(absl::string_view url) { return absl::StartsWith(url, TCP_SCHEME); }

bool Utility::urlIsUdpScheme(absl::string_view url) { return absl::StartsWith(url, UDP_SCHEME); }

bool Utility::urlIsUnixScheme(absl::string_view url) { return absl::StartsWith(url, UNIX_SCHEME); }

namespace {

Api::IoCallUint64Result receiveMessage(uint64_t max_rx_datagram_size, Buffer::InstancePtr& buffer,
                                       IoHandle::RecvMsgOutput& output, IoHandle& handle,
                                       const Address::Instance& local_address) {

  auto reservation = buffer->reserveSingleSlice(max_rx_datagram_size);
  Buffer::RawSlice slice = reservation.slice();
  Api::IoCallUint64Result result = handle.recvmsg(&slice, 1, local_address.ip()->port(), output);

  if (result.ok()) {
    reservation.commit(std::min(max_rx_datagram_size, result.return_value_));
  }

  return result;
}

StatusOr<sockaddr_in> parseV4Address(const std::string& ip_address, uint16_t port) {
  sockaddr_in sa4;
  memset(&sa4, 0, sizeof(sa4));
  if (inet_pton(AF_INET, ip_address.c_str(), &sa4.sin_addr) != 1) {
    return absl::FailedPreconditionError("failed parsing ipv4");
  }
  sa4.sin_family = AF_INET;
  sa4.sin_port = htons(port);
  return sa4;
}

StatusOr<sockaddr_in6> parseV6Address(const std::string& ip_address, uint16_t port) {
  sockaddr_in6 sa6;
  memset(&sa6, 0, sizeof(sa6));
  const auto scope_pos = ip_address.rfind('%');
  // TODO(#23952): Even though it would be nice to do any IPv6 parsing only with the getaddrinfo at
  // the moment Windows parsing is slightly different in behavior than other platforms. For this
  // reason we use the inet_pton for any parsing that does not contain the zone id.
  if (scope_pos == std::string::npos) {
    // Parse IPv6 with no scope.
    if (inet_pton(AF_INET6, ip_address.c_str(), &sa6.sin6_addr) != 1) {
      return absl::FailedPreconditionError("failed parsing ipv6");
    }
    sa6.sin6_family = AF_INET6;
  } else {
    // Parse IPv6 with optional scope using getaddrinfo().
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    struct addrinfo* res = nullptr;
    // Suppresses any potentially lengthy network host address lookups and inhibit the invocation of
    // a name resolution service.
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    hints.ai_family = AF_INET6;
    // Given that we don't specify a service but we use getaddrinfo() to only parse the node
    // address, specifying the socket type allows to hint the getaddrinfo() to return only an
    // element with the below socket type. The behavior though remains platform dependent and anyway
    // we consume only the first element (if the call succeeds).
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    const Api::SysCallIntResult rc = Api::OsSysCallsSingleton::get().getaddrinfo(
        ip_address.c_str(), /*service=*/nullptr, &hints, &res);
    if (rc.return_value_ != 0) {
      return absl::FailedPreconditionError(fmt::format("getaddrinfo error: {}", rc.return_value_));
    }
    sa6 = *reinterpret_cast<sockaddr_in6*>(res->ai_addr);
    freeaddrinfo(res);
  }
  sa6.sin6_port = htons(port);
  return sa6;
}

} // namespace

Address::InstanceConstSharedPtr Utility::parseInternetAddressNoThrow(const std::string& ip_address,
                                                                     uint16_t port, bool v6only) {
  StatusOr<sockaddr_in> sa4 = parseV4Address(ip_address, port);
  if (sa4.ok()) {
    return instanceOrNull(
        Address::InstanceFactory::createInstancePtr<Address::Ipv4Instance>(&sa4.value()));
  }

  StatusOr<sockaddr_in6> sa6 = parseV6Address(ip_address, port);
  if (sa6.ok()) {
    return instanceOrNull(
        Address::InstanceFactory::createInstancePtr<Address::Ipv6Instance>(*sa6, v6only));
  }
  return nullptr;
}

Address::InstanceConstSharedPtr Utility::parseInternetAddress(const std::string& ip_address,
                                                              uint16_t port, bool v6only) {
  const Address::InstanceConstSharedPtr address =
      parseInternetAddressNoThrow(ip_address, port, v6only);
  if (address == nullptr) {
    throwWithMalformedIp(ip_address);
  }
  return address;
}

Address::InstanceConstSharedPtr
Utility::parseInternetAddressAndPortNoThrow(const std::string& ip_address, bool v6only) {
  if (ip_address.empty()) {
    return nullptr;
  }
  if (ip_address[0] == '[') {
    // Appears to be an IPv6 address. Find the "]:" that separates the address from the port.
    const auto pos = ip_address.rfind("]:");
    if (pos == std::string::npos) {
      return nullptr;
    }
    const auto ip_str = ip_address.substr(1, pos - 1);
    const auto port_str = ip_address.substr(pos + 2);
    uint64_t port64 = 0;
    if (port_str.empty() || !absl::SimpleAtoi(port_str, &port64) || port64 > 65535) {
      return nullptr;
    }
    StatusOr<sockaddr_in6> sa6 = parseV6Address(ip_str, port64);
    if (sa6.ok()) {
      return instanceOrNull(
          Address::InstanceFactory::createInstancePtr<Address::Ipv6Instance>(*sa6, v6only));
    }
    return nullptr;
  }
  // Treat it as an IPv4 address followed by a port.
  const auto pos = ip_address.rfind(':');
  if (pos == std::string::npos) {
    return nullptr;
  }
  const auto ip_str = ip_address.substr(0, pos);
  const auto port_str = ip_address.substr(pos + 1);
  uint64_t port64 = 0;
  if (port_str.empty() || !absl::SimpleAtoi(port_str, &port64) || port64 > 65535) {
    return nullptr;
  }
  StatusOr<sockaddr_in> sa4 = parseV4Address(ip_str, port64);
  if (sa4.ok()) {
    return instanceOrNull(
        Address::InstanceFactory::createInstancePtr<Address::Ipv4Instance>(&sa4.value()));
  }
  return nullptr;
}

Address::InstanceConstSharedPtr Utility::parseInternetAddressAndPort(const std::string& ip_address,
                                                                     bool v6only) {

  const Address::InstanceConstSharedPtr address =
      parseInternetAddressAndPortNoThrow(ip_address, v6only);
  if (address == nullptr) {
    throwWithMalformedIp(ip_address);
  }
  return address;
}

Address::InstanceConstSharedPtr Utility::copyInternetAddressAndPort(const Address::Ip& ip) {
  if (ip.version() == Address::IpVersion::v4) {
    return std::make_shared<Address::Ipv4Instance>(ip.addressAsString(), ip.port());
  }
  return std::make_shared<Address::Ipv6Instance>(ip.addressAsString(), ip.port());
}

void Utility::throwWithMalformedIp(absl::string_view ip_address) {
  throw EnvoyException(absl::StrCat("malformed IP address: ", ip_address));
}

// TODO(hennna): Currently getLocalAddress does not support choosing between
// multiple interfaces and addresses not returned by getifaddrs. In addition,
// the default is to return a loopback address of type version. This function may
// need to be updated in the future. Discussion can be found at Github issue #939.
Address::InstanceConstSharedPtr Utility::getLocalAddress(const Address::IpVersion version) {
  Address::InstanceConstSharedPtr ret;
  if (Api::OsSysCallsSingleton::get().supportsGetifaddrs()) {
    Api::InterfaceAddressVector interface_addresses{};

    const Api::SysCallIntResult rc =
        Api::OsSysCallsSingleton::get().getifaddrs(interface_addresses);
    RELEASE_ASSERT(!rc.return_value_, fmt::format("getifaddrs error: {}", rc.errno_));

    // man getifaddrs(3)
    for (const auto& interface_address : interface_addresses) {
      if (!isLoopbackAddress(*interface_address.interface_addr_) &&
          interface_address.interface_addr_->ip()->version() == version) {
        ret = interface_address.interface_addr_;
        break;
      }
    }
  }

  // If the local address is not found above, then return the loopback address by default.
  if (ret == nullptr) {
    if (version == Address::IpVersion::v4) {
      ret = std::make_shared<Address::Ipv4Instance>("127.0.0.1");
    } else if (version == Address::IpVersion::v6) {
      ret = std::make_shared<Address::Ipv6Instance>("::1");
    }
  }
  return ret;
}

bool Utility::isSameIpOrLoopback(const ConnectionInfoProvider& connection_info_provider) {
  // These are local:
  // - Pipes
  // - Sockets to a loopback address
  // - Sockets where the local and remote address (ignoring port) are the same
  const auto& remote_address = connection_info_provider.remoteAddress();
  if (remote_address->type() == Address::Type::Pipe || isLoopbackAddress(*remote_address)) {
    return true;
  }
  const auto local_ip = connection_info_provider.localAddress()->ip();
  const auto remote_ip = remote_address->ip();
  if (remote_ip != nullptr && local_ip != nullptr &&
      remote_ip->addressAsString() == local_ip->addressAsString()) {
    return true;
  }
  return false;
}

bool Utility::isInternalAddress(const Address::Instance& address) {
  if (address.type() != Address::Type::Ip) {
    return false;
  }

  if (address.ip()->version() == Address::IpVersion::v4) {
    // Handle the RFC1918 space for IPV4. Also count loopback as internal.
    const uint32_t address4 = address.ip()->ipv4()->address();
    const uint8_t* address4_bytes = reinterpret_cast<const uint8_t*>(&address4);
    if ((address4_bytes[0] == 10) || (address4_bytes[0] == 192 && address4_bytes[1] == 168) ||
        (address4_bytes[0] == 172 && address4_bytes[1] >= 16 && address4_bytes[1] <= 31) ||
        address4 == htonl(INADDR_LOOPBACK)) {
      return true;
    } else {
      return false;
    }
  }

  // Local IPv6 address prefix defined in RFC4193. Local addresses have prefix FC00::/7.
  // Currently, the FD00::/8 prefix is locally assigned and FC00::/8 may be defined in the
  // future.
  static_assert(sizeof(absl::uint128) == sizeof(in6addr_loopback),
                "sizeof(absl::uint128) != sizeof(in6addr_loopback)");
  const absl::uint128 address6 = address.ip()->ipv6()->address();
  const uint8_t* address6_bytes = reinterpret_cast<const uint8_t*>(&address6);
  if (address6_bytes[0] == 0xfd ||
      memcmp(&address6, &in6addr_loopback, sizeof(in6addr_loopback)) == 0) {
    return true;
  }

  return false;
}

bool Utility::isLoopbackAddress(const Address::Instance& address) {
  if (address.type() != Address::Type::Ip) {
    return false;
  }

  if (address.ip()->version() == Address::IpVersion::v4) {
    // Compare to the canonical v4 loopback address: 127.0.0.1.
    return address.ip()->ipv4()->address() == htonl(INADDR_LOOPBACK);
  } else if (address.ip()->version() == Address::IpVersion::v6) {
    static_assert(sizeof(absl::uint128) == sizeof(in6addr_loopback),
                  "sizeof(absl::uint128) != sizeof(in6addr_loopback)");
    absl::uint128 addr = address.ip()->ipv6()->address();
    return 0 == memcmp(&addr, &in6addr_loopback, sizeof(in6addr_loopback));
  }
  IS_ENVOY_BUG("unexpected address type");
  return false;
}

Address::InstanceConstSharedPtr Utility::getCanonicalIpv4LoopbackAddress() {
  CONSTRUCT_ON_FIRST_USE(Address::InstanceConstSharedPtr,
                         new Address::Ipv4Instance("127.0.0.1", 0, nullptr));
}

Address::InstanceConstSharedPtr Utility::getIpv6LoopbackAddress() {
  CONSTRUCT_ON_FIRST_USE(Address::InstanceConstSharedPtr,
                         new Address::Ipv6Instance("::1", 0, nullptr));
}

Address::InstanceConstSharedPtr Utility::getIpv4AnyAddress() {
  CONSTRUCT_ON_FIRST_USE(Address::InstanceConstSharedPtr,
                         new Address::Ipv4Instance(static_cast<uint32_t>(0)));
}

Address::InstanceConstSharedPtr Utility::getIpv6AnyAddress() {
  CONSTRUCT_ON_FIRST_USE(Address::InstanceConstSharedPtr,
                         new Address::Ipv6Instance(static_cast<uint32_t>(0)));
}

const std::string& Utility::getIpv4CidrCatchAllAddress() {
  CONSTRUCT_ON_FIRST_USE(std::string, "0.0.0.0/0");
}

const std::string& Utility::getIpv6CidrCatchAllAddress() {
  CONSTRUCT_ON_FIRST_USE(std::string, "::/0");
}

Address::InstanceConstSharedPtr Utility::getAddressWithPort(const Address::Instance& address,
                                                            uint32_t port) {
  switch (address.ip()->version()) {
  case Address::IpVersion::v4:
    return std::make_shared<Address::Ipv4Instance>(address.ip()->addressAsString(), port);
  case Address::IpVersion::v6:
    return std::make_shared<Address::Ipv6Instance>(address.ip()->addressAsString(), port);
  }
  PANIC("not handled");
}

Address::InstanceConstSharedPtr Utility::getOriginalDst(Socket& sock) {
#ifdef SOL_IP

  if (sock.addressType() != Address::Type::Ip) {
    return nullptr;
  }

  auto ipVersion = sock.ipVersion();
  if (!ipVersion.has_value()) {
    return nullptr;
  }

  SocketOptionName opt_dst;
  SocketOptionName opt_tp;
  if (*ipVersion == Address::IpVersion::v4) {
    opt_dst = ENVOY_SOCKET_SO_ORIGINAL_DST;
    opt_tp = ENVOY_SOCKET_IP_TRANSPARENT;
  } else {
    opt_dst = ENVOY_SOCKET_IP6T_SO_ORIGINAL_DST;
    opt_tp = ENVOY_SOCKET_IPV6_TRANSPARENT;
  }

  sockaddr_storage orig_addr;
  memset(&orig_addr, 0, sizeof(orig_addr));
  socklen_t addr_len = sizeof(sockaddr_storage);
  int status =
      sock.getSocketOption(opt_dst.level(), opt_dst.option(), &orig_addr, &addr_len).return_value_;

  if (status != 0) {
    if (Api::OsSysCallsSingleton::get().supportsIpTransparent()) {
      socklen_t flag_len = sizeof(int);
      int is_tp;
      status =
          sock.getSocketOption(opt_tp.level(), opt_tp.option(), &is_tp, &flag_len).return_value_;
      if (status == 0 && is_tp) {
        return sock.ioHandle().localAddress();
      }
    }
    return nullptr;
  }

  return Address::addressFromSockAddrOrDie(orig_addr, 0, -1, true /* default for v6 constructor */);

#else
  // TODO(zuercher): determine if connection redirection is possible under macOS (c.f. pfctl and
  // divert), and whether it's possible to find the learn destination address.
  UNREFERENCED_PARAMETER(sock);
  return nullptr;
#endif
}

void Utility::parsePortRangeList(absl::string_view string, std::list<PortRange>& list) {
  const auto ranges = StringUtil::splitToken(string, ",");
  for (const auto& s : ranges) {
    const std::string s_string{s};
    std::stringstream ss(s_string);
    uint32_t min = 0;
    uint32_t max = 0;

    if (absl::StrContains(s, '-')) {
      char dash = 0;
      ss >> min;
      ss >> dash;
      ss >> max;
    } else {
      ss >> min;
      max = min;
    }

    if (s.empty() || (min > 65535) || (max > 65535) || ss.fail() || !ss.eof()) {
      throw EnvoyException(fmt::format("invalid port number or range '{}'", s_string));
    }

    list.emplace_back(PortRange(min, max));
  }
}

bool Utility::portInRangeList(const Address::Instance& address, const std::list<PortRange>& list) {
  if (address.type() != Address::Type::Ip) {
    return false;
  }

  for (const PortRange& p : list) {
    if (p.contains(address.ip()->port())) {
      return true;
    }
  }
  return false;
}

absl::uint128 Utility::Ip6ntohl(const absl::uint128& address) {
#ifdef ABSL_IS_LITTLE_ENDIAN
  return flipOrder(address);
#else
  return address;
#endif
}

absl::uint128 Utility::Ip6htonl(const absl::uint128& address) {
#ifdef ABSL_IS_LITTLE_ENDIAN
  return flipOrder(address);
#else
  return address;
#endif
}

absl::uint128 Utility::flipOrder(const absl::uint128& input) {
  absl::uint128 result{0};
  absl::uint128 data = input;
  for (int i = 0; i < 16; i++) {
    result <<= 8;
    result |= (data & 0x000000000000000000000000000000FF);
    data >>= 8;
  }
  return result;
}

Address::InstanceConstSharedPtr
Utility::protobufAddressToAddress(const envoy::config::core::v3::Address& proto_address) {
  switch (proto_address.address_case()) {
  case envoy::config::core::v3::Address::AddressCase::kSocketAddress:
    return Utility::parseInternetAddress(proto_address.socket_address().address(),
                                         proto_address.socket_address().port_value(),
                                         !proto_address.socket_address().ipv4_compat());
  case envoy::config::core::v3::Address::AddressCase::kPipe:
    return std::make_shared<Address::PipeInstance>(proto_address.pipe().path(),
                                                   proto_address.pipe().mode());
  case envoy::config::core::v3::Address::AddressCase::kEnvoyInternalAddress:
    PANIC("internal address not supported"); // TODO(lambdai) fix.
  case envoy::config::core::v3::Address::AddressCase::ADDRESS_NOT_SET:
    PANIC_DUE_TO_PROTO_UNSET;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

void Utility::addressToProtobufAddress(const Address::Instance& address,
                                       envoy::config::core::v3::Address& proto_address) {
  if (address.type() == Address::Type::Pipe) {
    proto_address.mutable_pipe()->set_path(address.asString());
  } else if (address.type() == Address::Type::Ip) {
    auto* socket_address = proto_address.mutable_socket_address();
    socket_address->set_address(address.ip()->addressAsString());
    socket_address->set_port_value(address.ip()->port());
  } else {
    ASSERT(address.type() == Address::Type::EnvoyInternal);
    auto* internal_address = proto_address.mutable_envoy_internal_address();
    internal_address->set_server_listener_name(address.envoyInternalAddress()->addressId());
    internal_address->set_endpoint_id(address.envoyInternalAddress()->endpointId());
  }
}

Socket::Type
Utility::protobufAddressSocketType(const envoy::config::core::v3::Address& proto_address) {
  switch (proto_address.address_case()) {
  case envoy::config::core::v3::Address::AddressCase::kSocketAddress: {
    const auto protocol = proto_address.socket_address().protocol();
    switch (protocol) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::config::core::v3::SocketAddress::TCP:
      return Socket::Type::Stream;
    case envoy::config::core::v3::SocketAddress::UDP:
      return Socket::Type::Datagram;
    }
  }
    PANIC_DUE_TO_CORRUPT_ENUM;
  case envoy::config::core::v3::Address::AddressCase::kPipe:
    return Socket::Type::Stream;
  case envoy::config::core::v3::Address::AddressCase::kEnvoyInternalAddress:
    // Currently internal address supports stream operation only.
    return Socket::Type::Stream;
  case envoy::config::core::v3::Address::AddressCase::ADDRESS_NOT_SET:
    PANIC_DUE_TO_PROTO_UNSET;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

Api::IoCallUint64Result Utility::writeToSocket(IoHandle& handle, const Buffer::Instance& buffer,
                                               const Address::Ip* local_ip,
                                               const Address::Instance& peer_address) {
  Buffer::RawSliceVector slices = buffer.getRawSlices();
  return writeToSocket(handle, slices.data(), slices.size(), local_ip, peer_address);
}

Api::IoCallUint64Result Utility::writeToSocket(IoHandle& handle, Buffer::RawSlice* slices,
                                               uint64_t num_slices, const Address::Ip* local_ip,
                                               const Address::Instance& peer_address) {
  Api::IoCallUint64Result send_result(
      /*rc=*/0, /*err=*/Api::IoErrorPtr(nullptr, IoSocketError::deleteIoError));
  do {
    send_result = handle.sendmsg(slices, num_slices, 0, local_ip, peer_address);
  } while (!send_result.ok() &&
           // Send again if interrupted.
           send_result.err_->getErrorCode() == Api::IoError::IoErrorCode::Interrupt);

  if (send_result.ok()) {
    ENVOY_LOG_MISC(trace, "sendmsg bytes {}", send_result.return_value_);
  } else {
    ENVOY_LOG_MISC(debug, "sendmsg failed with error code {}: {}",
                   static_cast<int>(send_result.err_->getErrorCode()),
                   send_result.err_->getErrorDetails());
  }
  return send_result;
}

void passPayloadToProcessor(uint64_t bytes_read, Buffer::InstancePtr buffer,
                            Address::InstanceConstSharedPtr peer_addess,
                            Address::InstanceConstSharedPtr local_address,
                            UdpPacketProcessor& udp_packet_processor, MonotonicTime receive_time) {
  RELEASE_ASSERT(
      peer_addess != nullptr,
      fmt::format("Unable to get remote address on the socket bount to local address: {} ",
                  local_address->asString()));

  // Unix domain sockets are not supported
  RELEASE_ASSERT(peer_addess->type() == Address::Type::Ip,
                 fmt::format("Unsupported remote address: {} local address: {}, receive size: "
                             "{}",
                             peer_addess->asString(), local_address->asString(), bytes_read));
  udp_packet_processor.processPacket(std::move(local_address), std::move(peer_addess),
                                     std::move(buffer), receive_time);
}

Api::IoCallUint64Result Utility::readFromSocket(IoHandle& handle,
                                                const Address::Instance& local_address,
                                                UdpPacketProcessor& udp_packet_processor,
                                                MonotonicTime receive_time, bool use_gro,
                                                uint32_t* packets_dropped) {

  if (use_gro) {
    Buffer::InstancePtr buffer = std::make_unique<Buffer::OwnedImpl>();
    IoHandle::RecvMsgOutput output(1, packets_dropped);

    // TODO(yugant): Avoid allocating 24k for each read by getting memory from UdpPacketProcessor
    const uint64_t max_rx_datagram_size_with_gro =
        NUM_DATAGRAMS_PER_RECEIVE * udp_packet_processor.maxDatagramSize();
    ENVOY_LOG_MISC(trace, "starting gro recvmsg with max={}", max_rx_datagram_size_with_gro);

    Api::IoCallUint64Result result =
        receiveMessage(max_rx_datagram_size_with_gro, buffer, output, handle, local_address);

    if (!result.ok() || output.msg_[0].truncated_and_dropped_) {
      return result;
    }

    const uint64_t gso_size = output.msg_[0].gso_size_;
    ENVOY_LOG_MISC(trace, "gro recvmsg bytes {} with gso_size as {}", result.return_value_,
                   gso_size);

    // Skip gso segmentation and proceed as a single payload.
    if (gso_size == 0u) {
      passPayloadToProcessor(
          result.return_value_, std::move(buffer), std::move(output.msg_[0].peer_address_),
          std::move(output.msg_[0].local_address_), udp_packet_processor, receive_time);
      return result;
    }

    // Segment the buffer read by the recvmsg syscall into gso_sized sub buffers.
    // TODO(mattklein123): The following code should be optimized to avoid buffer copies, either by
    // switching to slices or by using a CoW buffer type.
    while (buffer->length() > 0) {
      const uint64_t bytes_to_copy = std::min(buffer->length(), gso_size);
      Buffer::InstancePtr sub_buffer = std::make_unique<Buffer::OwnedImpl>();
      sub_buffer->move(*buffer, bytes_to_copy);
      passPayloadToProcessor(bytes_to_copy, std::move(sub_buffer), output.msg_[0].peer_address_,
                             output.msg_[0].local_address_, udp_packet_processor, receive_time);
    }

    return result;
  }

  if (handle.supportsMmsg()) {
    const auto max_rx_datagram_size = udp_packet_processor.maxDatagramSize();

    // Buffer::ReservationSingleSlice is always passed by value, and can only be constructed
    // by Buffer::Instance::reserve(), so this is needed to keep a fixed array
    // in which all elements are legally constructed.
    struct BufferAndReservation {
      BufferAndReservation(uint64_t max_rx_datagram_size)
          : buffer_(std::make_unique<Buffer::OwnedImpl>()),
            reservation_(buffer_->reserveSingleSlice(max_rx_datagram_size, true)) {}

      Buffer::InstancePtr buffer_;
      Buffer::ReservationSingleSlice reservation_;
    };
    constexpr uint32_t num_slices_per_packet = 1u;
    absl::InlinedVector<BufferAndReservation, NUM_DATAGRAMS_PER_RECEIVE> buffers;
    RawSliceArrays slices(NUM_DATAGRAMS_PER_RECEIVE,
                          absl::FixedArray<Buffer::RawSlice>(num_slices_per_packet));
    for (uint32_t i = 0; i < NUM_DATAGRAMS_PER_RECEIVE; i++) {
      buffers.push_back(max_rx_datagram_size);
      slices[i][0] = buffers[i].reservation_.slice();
    }

    IoHandle::RecvMsgOutput output(NUM_DATAGRAMS_PER_RECEIVE, packets_dropped);
    ENVOY_LOG_MISC(trace, "starting recvmmsg with packets={} max={}", NUM_DATAGRAMS_PER_RECEIVE,
                   max_rx_datagram_size);
    Api::IoCallUint64Result result = handle.recvmmsg(slices, local_address.ip()->port(), output);
    if (!result.ok()) {
      return result;
    }

    uint64_t packets_read = result.return_value_;
    ENVOY_LOG_MISC(trace, "recvmmsg read {} packets", packets_read);
    for (uint64_t i = 0; i < packets_read; ++i) {
      if (output.msg_[i].truncated_and_dropped_) {
        continue;
      }

      Buffer::RawSlice* slice = slices[i].data();
      const uint64_t msg_len = output.msg_[i].msg_len_;
      ASSERT(msg_len <= slice->len_);
      ENVOY_LOG_MISC(debug, "Receive a packet with {} bytes from {}", msg_len,
                     output.msg_[i].peer_address_->asString());

      buffers[i].reservation_.commit(std::min(max_rx_datagram_size, msg_len));

      passPayloadToProcessor(msg_len, std::move(buffers[i].buffer_), output.msg_[i].peer_address_,
                             output.msg_[i].local_address_, udp_packet_processor, receive_time);
    }
    return result;
  }

  Buffer::InstancePtr buffer = std::make_unique<Buffer::OwnedImpl>();
  IoHandle::RecvMsgOutput output(1, packets_dropped);

  ENVOY_LOG_MISC(trace, "starting recvmsg with max={}", udp_packet_processor.maxDatagramSize());
  Api::IoCallUint64Result result =
      receiveMessage(udp_packet_processor.maxDatagramSize(), buffer, output, handle, local_address);

  if (!result.ok() || output.msg_[0].truncated_and_dropped_) {
    return result;
  }

  ENVOY_LOG_MISC(trace, "recvmsg bytes {}", result.return_value_);

  passPayloadToProcessor(
      result.return_value_, std::move(buffer), std::move(output.msg_[0].peer_address_),
      std::move(output.msg_[0].local_address_), udp_packet_processor, receive_time);
  return result;
}

Api::IoErrorPtr Utility::readPacketsFromSocket(IoHandle& handle,
                                               const Address::Instance& local_address,
                                               UdpPacketProcessor& udp_packet_processor,
                                               TimeSource& time_source, bool prefer_gro,
                                               uint32_t& packets_dropped) {
  // Read at least one time, and attempt to read numPacketsExpectedPerEventLoop() packets unless
  // this goes over MAX_NUM_PACKETS_PER_EVENT_LOOP.
  size_t num_packets_to_read = std::min<size_t>(
      MAX_NUM_PACKETS_PER_EVENT_LOOP, udp_packet_processor.numPacketsExpectedPerEventLoop());
  const bool use_gro = prefer_gro && handle.supportsUdpGro();
  size_t num_reads =
      use_gro ? (num_packets_to_read / NUM_DATAGRAMS_PER_RECEIVE)
              : (handle.supportsMmsg() ? (num_packets_to_read / NUM_DATAGRAMS_PER_RECEIVE)
                                       : num_packets_to_read);
  // Make sure to read at least once.
  num_reads = std::max<size_t>(1, num_reads);
  do {
    const uint32_t old_packets_dropped = packets_dropped;
    const MonotonicTime receive_time = time_source.monotonicTime();
    Api::IoCallUint64Result result = Utility::readFromSocket(
        handle, local_address, udp_packet_processor, receive_time, use_gro, &packets_dropped);

    if (!result.ok()) {
      // No more to read or encountered a system error.
      return std::move(result.err_);
    }

    if (packets_dropped != old_packets_dropped) {
      // The kernel tracks SO_RXQ_OVFL as a uint32 which can overflow to a smaller
      // value. So as long as this count differs from previously recorded value,
      // more packets are dropped by kernel.
      const uint32_t delta =
          (packets_dropped > old_packets_dropped)
              ? (packets_dropped - old_packets_dropped)
              : (packets_dropped + (std::numeric_limits<uint32_t>::max() - old_packets_dropped) +
                 1);
      ENVOY_LOG_EVERY_POW_2_MISC(
          warn,
          "Kernel dropped {} datagram(s). Consider increasing receive buffer size and/or "
          "max datagram size.",
          delta);
      udp_packet_processor.onDatagramsDropped(delta);
    }
    --num_reads;
    if (num_reads == 0) {
      return std::move(result.err_);
    }
  } while (true);
}

ResolvedUdpSocketConfig::ResolvedUdpSocketConfig(
    const envoy::config::core::v3::UdpSocketConfig& config, bool prefer_gro_default)
    : max_rx_datagram_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, max_rx_datagram_size,
                                                            DEFAULT_UDP_MAX_DATAGRAM_SIZE)),
      prefer_gro_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, prefer_gro, prefer_gro_default)) {
  if (prefer_gro_ && !Api::OsSysCallsSingleton::get().supportsUdpGro()) {
    ENVOY_LOG_MISC(
        warn, "GRO requested but not supported by the OS. Check OS config or disable prefer_gro.");
  }
}

} // namespace Network
} // namespace Envoy
