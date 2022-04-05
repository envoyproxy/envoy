#pragma once

#include "envoy/network/filter.h"

namespace Envoy {
namespace Network {
namespace Matching {

/**
 * Implementation of Network::MatchingData, providing connection-level data to
 * the match tree.
 */
class MatchingDataImpl : public MatchingData {
public:
  explicit MatchingDataImpl(const ConnectionSocket& socket) : socket_(socket) {}
  const ConnectionSocket& socket() const override { return socket_; }

private:
  const ConnectionSocket& socket_;
};

/**
 * Implementation of Network::UdpMatchingData, providing UDP data to the match tree.
 */
class UdpMatchingDataImpl : public UdpMatchingData {
public:
  UdpMatchingDataImpl(const Address::InstanceConstSharedPtr& local_address,
                      const Address::InstanceConstSharedPtr& remote_address)
      : local_address_(local_address), remote_address_(remote_address) {}
  const Address::InstanceConstSharedPtr& localAddress() const override { return local_address_; }
  const Address::InstanceConstSharedPtr& remoteAddress() const override { return remote_address_; }

private:
  const Address::InstanceConstSharedPtr& local_address_;
  const Address::InstanceConstSharedPtr& remote_address_;
};

} // namespace Matching
} // namespace Network
} // namespace Envoy
