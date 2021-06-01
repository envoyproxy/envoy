#pragma once

#include "envoy/network/address.h"

#include "absl/types/optional.h"

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
   * @return absl::optional<uint64_t> an optional hash value to route on. A hash value might not be
   * returned if for example the downstream address has a unix domain socket type.
   */
  virtual absl::optional<uint64_t>
  generateHash(const Network::Address::Instance& downstream_address) const PURE;
};
} // namespace Udp
} // namespace Envoy
