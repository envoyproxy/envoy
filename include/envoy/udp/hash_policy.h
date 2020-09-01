#pragma once

#include "envoy/network/address.h"

namespace Envoy {
namespace Udp {
/**
 * Hash policy for UDP transport layer protocol.
 */
class HashPolicy {
public:
  virtual ~HashPolicy() = default;

  /**
   * @param downstream_address is the address of the peer client.
   * @return uint64_t a hash value to route on.
   */
  virtual uint64_t generateHash(const Network::Address::Instance& downstream_address) const PURE;
};
} // namespace Udp
} // namespace Envoy
