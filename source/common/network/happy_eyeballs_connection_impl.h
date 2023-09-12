#pragma once

#include "envoy/upstream/upstream.h"

#include "source/common/network/multi_connection_base_impl.h"

namespace Envoy {
namespace Network {

/**
 * Implementation of ConnectionProvider for HappyEyeballs. It provides client
 * connections to multiple addresses in an specific order complying to
 * HappyEyeballs.
 */
class HappyEyeballsConnectionProvider : public ConnectionProvider,
                                        Logger::Loggable<Logger::Id::happy_eyeballs> {
public:
  HappyEyeballsConnectionProvider(
      Event::Dispatcher& dispatcher,
      const std::vector<Address::InstanceConstSharedPtr>& address_list,
      const std::shared_ptr<const Upstream::UpstreamLocalAddressSelector>&
          upstream_local_address_selector,
      UpstreamTransportSocketFactory& socket_factory,
      TransportSocketOptionsConstSharedPtr transport_socket_options,
      const Upstream::HostDescriptionConstSharedPtr& host,
      const ConnectionSocket::OptionsSharedPtr options);
  bool hasNextConnection() override;
  ClientConnectionPtr createNextConnection(const uint64_t id) override;
  size_t nextConnection() override;
  size_t totalConnections() override;
  // Returns a new vector containing the contents of |address_list| sorted
  // with address families interleaved, as per Section 4 of RFC 8305, Happy
  // Eyeballs v2. It is assumed that the list must already be sorted as per
  // Section 6 of RFC6724, which happens in the DNS implementations (ares_getaddrinfo()
  // and Apple DNS).
  static std::vector<Address::InstanceConstSharedPtr>
  sortAddresses(const std::vector<Address::InstanceConstSharedPtr>& address_list);

private:
  Event::Dispatcher& dispatcher_;
  // List of addresses to attempt to connect to.
  const std::vector<Address::InstanceConstSharedPtr> address_list_;
  const Upstream::UpstreamLocalAddressSelectorConstSharedPtr upstream_local_address_selector_;
  UpstreamTransportSocketFactory& socket_factory_;
  TransportSocketOptionsConstSharedPtr transport_socket_options_;
  const Upstream::HostDescriptionConstSharedPtr host_;
  const ConnectionSocket::OptionsSharedPtr options_;
  // Index of the next address to use.
  size_t next_address_ = 0;
  // True if the first connection has been created.
  bool first_connection_created_ = false;
};

/**
 * Implementation of ClientConnection which transparently attempts connections to
 * multiple different IP addresses, and uses the first connection that succeeds.
 * After a connection is established, all methods simply delegate to the
 * underlying connection. However, before the connection is established
 * their behavior depends on their semantics. For anything which can result
 * in up-call (e.g. filter registration) or which must only happen once (e.g.
 * writing data) the context is saved in until the connection completes, at
 * which point they are replayed to the underlying connection. For simple methods
 * they are applied to each open connection and applied when creating new ones.
 *
 * See the Happy Eyeballs RFC at https://datatracker.ietf.org/doc/html/rfc6555
 * TODO(RyanTheOptimist): Implement the Happy Eyeballs address sorting algorithm
 * either in the class or in the resolution code.
 */
class HappyEyeballsConnectionImpl : public MultiConnectionBaseImpl,
                                    Logger::Loggable<Logger::Id::happy_eyeballs> {
public:
  HappyEyeballsConnectionImpl(Event::Dispatcher& dispatcher,
                              const std::vector<Address::InstanceConstSharedPtr>& address_list,
                              const std::shared_ptr<const Upstream::UpstreamLocalAddressSelector>&
                                  upstream_local_address_selector,
                              UpstreamTransportSocketFactory& socket_factory,
                              TransportSocketOptionsConstSharedPtr transport_socket_options,
                              const Upstream::HostDescriptionConstSharedPtr& host,
                              const ConnectionSocket::OptionsSharedPtr options)
      : MultiConnectionBaseImpl(dispatcher,
                                std::make_unique<Network::HappyEyeballsConnectionProvider>(
                                    dispatcher, address_list, upstream_local_address_selector,
                                    socket_factory, transport_socket_options, host, options)) {}
};

} // namespace Network
} // namespace Envoy
