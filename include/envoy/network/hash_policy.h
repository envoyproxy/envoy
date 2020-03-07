#pragma once

#include "envoy/network/address.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Network {
/**
 * Hash policy for transport layer protocol.
 */
class HashPolicy {
public:
  virtual ~HashPolicy() = default;

  /**
   * @param downstream_address is the address of the connected client.
   * @param upstream_address is the address of the connected server.
   * @return absl::optional<uint64_t> an optional hash value to route on. A hash value might not be
   * returned if for example the downstream address is nullptr.
   */
  virtual absl::optional<uint64_t>
  generateHash(const Network::Address::Instance* downstream_address,
               const Network::Address::Instance* upstream_address) const PURE;
};
} // namespace Network
} // namespace Envoy
