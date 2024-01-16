#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/listener.h"

#include "source/common/common/statusor.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Network {

/**
 * Utility class to represent TCP/UDP port range
 */
class PortRange {
public:
  PortRange(uint32_t min, uint32_t max) : min_(min), max_(max) {}

  bool contains(uint32_t port) const { return (port >= min_ && port <= max_); }

private:
  const uint32_t min_;
  const uint32_t max_;
};

using PortRangeList = std::list<PortRange>;

/**
 * A callback interface used by readFromSocket() to pass packets read from
 * socket.
 */
class UdpPacketProcessor {
public:
  virtual ~UdpPacketProcessor() = default;

  /**
   * Consume the packet read out of the socket with the information from UDP
   * header.
   * @param local_address is the destination address in the UDP header.
   * @param peer_address is the source address in the UDP header.
   * @param buffer contains the packet read.
   * @param receive_time is the time when the packet is read.
   */
  virtual void processPacket(Address::InstanceConstSharedPtr local_address,
                             Address::InstanceConstSharedPtr peer_address,
                             Buffer::InstancePtr buffer, MonotonicTime receive_time) PURE;

  /**
   * Called whenever datagrams are dropped due to overflow or truncation.
   * @param dropped supplies the number of dropped datagrams.
   */
  virtual void onDatagramsDropped(uint32_t dropped) PURE;

  /**
   * The expected max size of the datagram to be read. If it's smaller than
   * the size of datagrams received, they will be dropped.
   */
  virtual uint64_t maxDatagramSize() const PURE;

  /**
   * An estimated number of packets to read in each read event.
   */
  virtual size_t numPacketsExpectedPerEventLoop() const PURE;
};

static const uint64_t DEFAULT_UDP_MAX_DATAGRAM_SIZE = 1500;
static const uint64_t NUM_DATAGRAMS_PER_RECEIVE = 16;
static const uint64_t MAX_NUM_PACKETS_PER_EVENT_LOOP = 6000;

/**
 * Wrapper which resolves UDP socket proto config with defaults.
 */
struct ResolvedUdpSocketConfig {
  ResolvedUdpSocketConfig(const envoy::config::core::v3::UdpSocketConfig& config,
                          bool prefer_gro_default);

  uint64_t max_rx_datagram_size_;
  bool prefer_gro_;
};

/**
 * Common network utility routines.
 */
class Utility {
public:
  static constexpr absl::string_view TCP_SCHEME{"tcp://"};
  static constexpr absl::string_view UDP_SCHEME{"udp://"};
  static constexpr absl::string_view UNIX_SCHEME{"unix://"};

  /**
   * Make a URL from a datagram Address::Instance; will be udp:// prefix for
   * an IP address, and unix:// prefix otherwise. Giving a tcp address to this
   * function will result in incorrect behavior (addresses don't know if they
   * are datagram or stream).
   * @param addr supplies the address to convert to string.
   * @return The appropriate url string compatible with resolveUrl.
   */
  static std::string urlFromDatagramAddress(const Address::Instance& addr);

  /**
   * Resolve a URL.
   * @param url supplies the url to resolve.
   * @return Address::InstanceConstSharedPtr the resolved address.
   * @throw EnvoyException if url is invalid.
   */
  static Address::InstanceConstSharedPtr resolveUrl(const std::string& url);

  /**
   * Determine the socket type for a URL.
   *
   * @param url supplies the url to resolve.
   * @return StatusOr<Socket::Type> of the socket type, or an error status if url is invalid.
   */
  static StatusOr<Socket::Type> socketTypeFromUrl(const std::string& url);

  /**
   * Match a URL to the TCP scheme
   * @param url supplies the URL to match.
   * @return bool true if the URL matches the TCP scheme, false otherwise.
   */
  static bool urlIsTcpScheme(absl::string_view url);

  /**
   * Match a URL to the UDP scheme
   * @param url supplies the URL to match.
   * @return bool true if the URL matches the UDP scheme, false otherwise.
   */
  static bool urlIsUdpScheme(absl::string_view url);

  /**
   * Match a URL to the Unix scheme
   * @param url supplies the Unix to match.
   * @return bool true if the URL matches the Unix scheme, false otherwise.
   */
  static bool urlIsUnixScheme(absl::string_view url);

  /**
   * Parse an internet host address (IPv4 or IPv6) and create an Instance from it. The address must
   * not include a port number. Throws EnvoyException if unable to parse the address.
   * @param ip_address string to be parsed as an internet address.
   * @param port optional port to include in Instance created from ip_address, 0 by default.
   * @param v6only disable IPv4-IPv6 mapping for IPv6 addresses?
   * @return pointer to the Instance.
   * @throw EnvoyException in case of a malformed IP address.
   */
  static Address::InstanceConstSharedPtr
  parseInternetAddress(const std::string& ip_address, uint16_t port = 0, bool v6only = true);

  /**
   * Parse an internet host address (IPv4 or IPv6) and create an Instance from it. The address must
   * not include a port number.
   * @param ip_address string to be parsed as an internet address.
   * @param port optional port to include in Instance created from ip_address, 0 by default.
   * @param v6only disable IPv4-IPv6 mapping for IPv6 addresses?
   * @return pointer to the Instance, or nullptr if unable to parse the address.
   */
  static Address::InstanceConstSharedPtr
  parseInternetAddressNoThrow(const std::string& ip_address, uint16_t port = 0, bool v6only = true);

  /**
   * Parse an internet host address (IPv4 or IPv6) AND port, and create an Instance from it. Throws
   * EnvoyException if unable to parse the address. This is needed when a shared pointer is needed
   * but only a raw instance is available.
   * @param Address::Ip& to be copied to the new instance.
   * @return pointer to the Instance.
   */
  static Address::InstanceConstSharedPtr copyInternetAddressAndPort(const Address::Ip& ip);

  /**
   * Create a new Instance from an internet host address (IPv4 or IPv6) and port.
   * @param ip_addr string to be parsed as an internet address and port. Examples:
   *        - "1.2.3.4:80"
   *        - "[1234:5678::9]:443"
   * @param v6only disable IPv4-IPv6 mapping for IPv6 addresses?
   * @return pointer to the Instance.
   * @throw EnvoyException in case of a malformed IP address.
   */
  static Address::InstanceConstSharedPtr parseInternetAddressAndPort(const std::string& ip_address,
                                                                     bool v6only = true);

  /**
   * Create a new Instance from an internet host address (IPv4 or IPv6) and port.
   * @param ip_addr string to be parsed as an internet address and port. Examples:
   *        - "1.2.3.4:80"
   *        - "[1234:5678::9]:443"
   * @param v6only disable IPv4-IPv6 mapping for IPv6 addresses?
   * @return pointer to the Instance, or a nullptr in case of a malformed IP address.
   */
  static Address::InstanceConstSharedPtr
  parseInternetAddressAndPortNoThrow(const std::string& ip_address, bool v6only = true);

  /**
   * Get the local address of the first interface address that is of type
   * version and is not a loopback address. If no matches are found, return the
   * loopback address of type version.
   * @param the local address IP version.
   * @return the local IP address of the server
   */
  static Address::InstanceConstSharedPtr getLocalAddress(const Address::IpVersion version);

  /**
   * Determine whether this is a local connection.
   * @return bool the address is a local connection.
   */
  static bool isSameIpOrLoopback(const ConnectionInfoProvider& socket);

  /**
   * Determine whether this is an internal (RFC1918) address.
   * @return bool the address is an RFC1918 address.
   */
  static bool isInternalAddress(const Address::Instance& address);

  /**
   * Check if address is loopback address.
   * @param address IP address to check.
   * @return true if so, otherwise false
   */
  static bool isLoopbackAddress(const Address::Instance& address);

  /**
   * @return Address::InstanceConstSharedPtr an address that represents the canonical IPv4 loopback
   *         address (i.e. "127.0.0.1"). Note that the range "127.0.0.0/8" is all defined as the
   *         loopback range, but the address typically used (e.g. in tests) is "127.0.0.1".
   */
  static Address::InstanceConstSharedPtr getCanonicalIpv4LoopbackAddress();

  /**
   * @return Address::InstanceConstSharedPtr an address that represents the IPv6 loopback address
   *         (i.e. "::1").
   */
  static Address::InstanceConstSharedPtr getIpv6LoopbackAddress();

  /**
   * @return Address::InstanceConstSharedPtr an address that represents the IPv4 wildcard address
   *         (i.e. "0.0.0.0"). Used during binding to indicate that incoming connections to any
   *         local IPv4 address are to be accepted.
   */
  static Address::InstanceConstSharedPtr getIpv4AnyAddress();

  /**
   * @return Address::InstanceConstSharedPtr an address that represents the IPv6 wildcard address
   *         (i.e. "::"). Used during binding to indicate that incoming connections to any local
   *         IPv6 address are to be accepted.
   */
  static Address::InstanceConstSharedPtr getIpv6AnyAddress();

  /**
   * @return the IPv4 CIDR catch-all address (0.0.0.0/0).
   */
  static const std::string& getIpv4CidrCatchAllAddress();

  /**
   * @return the IPv6 CIDR catch-all address (::/0).
   */
  static const std::string& getIpv6CidrCatchAllAddress();

  /**
   * @param address IP address instance.
   * @param port to update.
   * @return Address::InstanceConstSharedPtr a new address instance with updated port.
   */
  static Address::InstanceConstSharedPtr getAddressWithPort(const Address::Instance& address,
                                                            uint32_t port);

  /**
   * Retrieve the original destination address from an accepted socket.
   * The address (IP and port) may be not local and the port may differ from
   * the listener port if the packets were redirected using iptables
   * @param sock is accepted socket
   * @return the original destination or nullptr if not available.
   */
  static Address::InstanceConstSharedPtr getOriginalDst(Socket& sock);

  /**
   * Parses a string containing a comma-separated list of port numbers and/or
   * port ranges and appends the values to a caller-provided list of PortRange structures.
   * For example, the string "1-1024,2048-4096,12345" causes 3 PortRange structures
   * to be appended to the supplied list.
   * @param str is the string containing the port numbers and ranges
   * @param list is the list to append the new data structures to
   */
  static void parsePortRangeList(absl::string_view string, std::list<PortRange>& list);

  /**
   * Checks whether a given port number appears in at least one of the port ranges in a list
   * @param address supplies the IP address to compare.
   * @param list the list of port ranges in which the port may appear
   * @return whether the port appears in at least one of the ranges in the list
   */
  static bool portInRangeList(const Address::Instance& address, const std::list<PortRange>& list);

  /**
   * Converts IPv6 absl::uint128 in network byte order to host byte order.
   * @param address supplies the IPv6 address in network byte order.
   * @return the absl::uint128 IPv6 address in host byte order.
   */
  static absl::uint128 Ip6ntohl(const absl::uint128& address);

  /**
   * Converts IPv6 absl::uint128 in host byte order to network byte order.
   * @param address supplies the IPv6 address in host byte order.
   * @return the absl::uint128 IPv6 address in network byte order.
   */
  static absl::uint128 Ip6htonl(const absl::uint128& address);

  static Address::InstanceConstSharedPtr
  protobufAddressToAddress(const envoy::config::core::v3::Address& proto_address);

  /**
   * Copies the address instance into the protobuf representation of an address.
   * @param address is the address to be copied into the protobuf representation of this address.
   * @param proto_address is the protobuf address to which the address instance is copied into.
   */
  static void addressToProtobufAddress(const Address::Instance& address,
                                       envoy::config::core::v3::Address& proto_address);

  /**
   * Returns socket type corresponding to SocketAddress.protocol value of the
   * given address, or SocketType::Stream if the address is a pipe address.
   * @param proto_address the address protobuf
   * @return socket type
   */
  static Socket::Type
  protobufAddressSocketType(const envoy::config::core::v3::Address& proto_address);

  /**
   * Send a packet via given UDP socket with specific source address.
   * @param handle is the UDP socket used to send.
   * @param slices points to the buffers containing the packet.
   * @param num_slices is the number of buffers.
   * @param local_ip is the source address to be used to send.
   * @param peer_address is the destination address to send to.
   */
  static Api::IoCallUint64Result writeToSocket(IoHandle& handle, Buffer::RawSlice* slices,
                                               uint64_t num_slices, const Address::Ip* local_ip,
                                               const Address::Instance& peer_address);
  static Api::IoCallUint64Result writeToSocket(IoHandle& handle, const Buffer::Instance& buffer,
                                               const Address::Ip* local_ip,
                                               const Address::Instance& peer_address);

  /**
   * Read a packet from a given UDP socket and pass the packet to given UdpPacketProcessor.
   * @param handle is the UDP socket to read from.
   * @param local_address is the socket's local address used to populate port.
   * @param udp_packet_processor is the callback to receive the packet.
   * @param receive_time is the timestamp passed to udp_packet_processor for the
   * receive time of the packet.
   * @param prefer_gro supplies whether to use GRO if the OS supports it.
   * @param packets_dropped is the output parameter for number of packets dropped in kernel. If the
   * caller is not interested in it, nullptr can be passed in.
   */
  static Api::IoCallUint64Result readFromSocket(IoHandle& handle,
                                                const Address::Instance& local_address,
                                                UdpPacketProcessor& udp_packet_processor,
                                                MonotonicTime receive_time, bool use_gro,
                                                uint32_t* packets_dropped);

  /**
   * Read some packets from a given UDP socket and pass the packet to a given
   * UdpPacketProcessor. Read no more than MAX_NUM_PACKETS_PER_EVENT_LOOP packets.
   * @param handle is the UDP socket to read from.
   * @param local_address is the socket's local address used to populate port.
   * @param udp_packet_processor is the callback to receive the packets.
   * @param time_source is the time source used to generate the time stamp of the received packets.
   * @param prefer_gro supplies whether to use GRO if the OS supports it.
   * @param packets_dropped is the output parameter for number of packets dropped in kernel.
   * Return the io error encountered or nullptr if no io error but read stopped
   * because of MAX_NUM_PACKETS_PER_EVENT_LOOP.
   *
   * TODO(mattklein123): Allow the number of packets read to be limited for fairness. Currently
   *                     this function will always return an error, even if EAGAIN. In the future
   *                     we can return no error if we limited the number of packets read and have
   *                     to fake another read event.
   * TODO(mattklein123): Can we potentially share this with the TCP stack somehow? Similar code
   *                     exists there.
   */
  static Api::IoErrorPtr readPacketsFromSocket(IoHandle& handle,
                                               const Address::Instance& local_address,
                                               UdpPacketProcessor& udp_packet_processor,
                                               TimeSource& time_source, bool prefer_gro,
                                               uint32_t& packets_dropped);

private:
  static void throwWithMalformedIp(absl::string_view ip_address);

  /**
   * Takes a number and flips the order in byte chunks. The last byte of the input will be the
   * first byte in the output. The second to last byte will be the second to first byte in the
   * output. Etc..
   * @param input supplies the input to have the bytes flipped.
   * @return the absl::uint128 of the input having the bytes flipped.
   */
  static absl::uint128 flipOrder(const absl::uint128& input);
};

/**
 * Log formatter for an address.
 */
struct AddressStrFormatter {
  void operator()(std::string* out, const Network::Address::InstanceConstSharedPtr& instance) {
    out->append(instance->asString());
  }
};

} // namespace Network
} // namespace Envoy
