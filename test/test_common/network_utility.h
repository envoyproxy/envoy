#pragma once

#include <list>
#include <string>

#include "envoy/network/address.h"
#include "envoy/network/filter.h"
#include "envoy/network/io_handle.h"
#include "envoy/network/transport_socket.h"

namespace Envoy {
namespace Network {
namespace Test {

/**
 * Determines if the passed in address and port is available for binding. If the port is zero,
 * the OS should pick an unused port for the supplied address (e.g. for the loopback address).
 * NOTE: this is racy, as it does not provide a means to keep the port reserved for the
 * caller's use.
 * @param addr_port a valid host address (e.g. an address of one of the network interfaces
 *        of this host, or the any address or the loopback address) and port (zero to indicate
 *        that the OS should pick an unused address.
 * @param type the type of socket to be tested.
 * @returns the address and port (selected if zero was the passed in port) that can be used for
 *          listening, else nullptr if the address and port are not free.
 */
Address::InstanceConstSharedPtr findOrCheckFreePort(Address::InstanceConstSharedPtr addr_port,
                                                    Socket::Type type);

/**
 * As above, but addr_port is specified as a string. For example:
 *    - 127.0.0.1:32000  Check whether a specific port on the IPv4 loopback address is free.
 *    - [::1]:0          Pick a free port on the IPv6 loopback address.
 *    - 0.0.0.0:0        Pick a free port on all local addresses of all local interfaces.
 *    - [::]:45678       Check whether a specific port on all local IPv6 addresses is free.
 */
Address::InstanceConstSharedPtr findOrCheckFreePort(const std::string& addr_port,
                                                    Socket::Type type);

/**
 * Get a URL ready IP loopback address as a string.
 * @param version IP address version of loopback address.
 * @return std::string URL ready loopback address as a string.
 */
std::string getLoopbackAddressUrlString(const Address::IpVersion version);

/**
 * Get a IP loopback address as a string. There are no square brackets around IPv6 addresses, this
 * is what inet_ntop() gives.
 * @param version IP address version of loopback address.
 * @return std::string loopback address as a string.
 */
std::string getLoopbackAddressString(const Address::IpVersion version);

/**
 * Get a URL ready IP any address as a string.
 * @param version IP address version of any address.
 * @return std::string URL ready any address as a string.
 */
std::string getAnyAddressUrlString(const Address::IpVersion version);

/**
 * Get an IP any address as a string.
 * @param version IP address version of any address.
 * @return std::string any address as a string.
 */
std::string getAnyAddressString(const Address::IpVersion version);

/**
 * Return a string version of enum IpVersion version.
 * @param version IP address version.
 * @return std::string string version of IpVersion.
 */
std::string addressVersionAsString(const Address::IpVersion version);

/**
 * Returns a loopback address for the specified IP version (127.0.0.1 for IPv4 and ::1 for IPv6).
 * @param version the IP version of the loopback address.
 * @returns a loopback address for the specified IP version.
 */
Address::InstanceConstSharedPtr getCanonicalLoopbackAddress(const Address::IpVersion version);

/**
 * Returns the any address for the specified IP version.
 * @param version the IP version of the any address.
 * @param v4_compat determines whether a v4-mapped addresses bound to a socket listening on the
 *        returned ANY address are to be treated as IPv4 or IPv6 addresses. Defaults to 'false',
 *        has no effect with IPv4 ANY address.
 * @returns the any address for the specified IP version.
 */
Address::InstanceConstSharedPtr getAnyAddress(const Address::IpVersion version,
                                              bool v4_compat = false);

/**
 * This function tries to create a socket of type IpVersion version and bind to it. If
 * successful this function returns true. If either socket creation or socket
 * bind fail, this function returns false.
 * @param version the IP version to test.
 * @return bool whether IpVersion addresses are "supported".
 */
bool supportsIpVersion(const Address::IpVersion version);

/**
 * Returns the DNS family for the specified IP version.
 * @param version the IP version of the DNS lookup family.
 */
std::string ipVersionToDnsFamily(Network::Address::IpVersion version);

/**
 * Bind a socket to a free port on a loopback address, and return the socket's fd and bound address.
 * Enables a test server to reliably "select" a port to listen on.
 * @param version the IP version of the loopback address.
 * @param type the type of socket to be bound.
 * @param reuse_port specifies whether the socket option SO_REUSEADDR has been set on the socket.
 * @returns the address and the fd of the socket bound to that address.
 */
std::pair<Address::InstanceConstSharedPtr, Network::SocketPtr>
bindFreeLoopbackPort(Address::IpVersion version, Socket::Type type, bool reuse_port = false);

/**
 * Create a transport socket for testing purposes.
 * @return TransportSocketPtr the transport socket factory to use with a connection.
 */
TransportSocketPtr createRawBufferSocket();

/**
 * Create a transport socket factory for testing purposes.
 * @return TransportSocketFactoryPtr the transport socket factory to use with a cluster or a
 * listener.
 */
TransportSocketFactoryPtr createRawBufferSocketFactory();

/**
 * Implementation of Network::FilterChain with empty filter chain, but pluggable transport socket
 * factory.
 */
class EmptyFilterChain : public FilterChain {
public:
  EmptyFilterChain(TransportSocketFactoryPtr&& transport_socket_factory)
      : transport_socket_factory_(std::move(transport_socket_factory)) {}

  // Network::FilterChain
  const TransportSocketFactory& transportSocketFactory() const override {
    return *transport_socket_factory_;
  }

  std::chrono::milliseconds transportSocketConnectTimeout() const override {
    return std::chrono::milliseconds::zero();
  }

  const std::vector<FilterFactoryCb>& networkFilterFactories() const override {
    return empty_network_filter_factory_;
  }

private:
  const TransportSocketFactoryPtr transport_socket_factory_;
  const std::vector<FilterFactoryCb> empty_network_filter_factory_{};
};

/**
 * Create an empty filter chain for testing purposes.
 * @param transport_socket_factory transport socket factory to use when creating transport sockets.
 * @return const FilterChainSharedPtr filter chain.
 */
const FilterChainSharedPtr
createEmptyFilterChain(TransportSocketFactoryPtr&& transport_socket_factory);

/**
 * Create an empty filter chain creating raw buffer sockets for testing purposes.
 * @return const FilterChainSharedPtr filter chain.
 */
const FilterChainSharedPtr createEmptyFilterChainWithRawBufferSockets();

/**
 * Wrapper for Utility::readFromSocket() which reads a single datagram into the supplied
 * UdpRecvData without worrying about the packet processor interface. The function will
 * instantiate the buffer returned in data.
 */
Api::IoCallUint64Result readFromSocket(IoHandle& handle, const Address::Instance& local_address,
                                       UdpRecvData& data);

/**
 * A synchronous UDP peer that can be used for testing.
 */
class UdpSyncPeer {
public:
  UdpSyncPeer(Network::Address::IpVersion version);

  // Writer a datagram to a remote peer.
  void write(const std::string& buffer, const Network::Address::Instance& peer);

  // Receive a datagram.
  void recv(Network::UdpRecvData& datagram);

  // Return the local peer's socket address.
  const Network::Address::InstanceConstSharedPtr& localAddress() { return socket_->localAddress(); }

private:
  const Network::SocketPtr socket_;
  std::list<Network::UdpRecvData> received_datagrams_;
};

} // namespace Test
} // namespace Network
} // namespace Envoy
