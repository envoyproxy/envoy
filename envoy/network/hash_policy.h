#pragma once

#include "envoy/network/connection.h"

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
   * @param connection is the raw downstream connection. Different implementations of HashPolicy can
   *        compute hashes based on different data accessible from the connection (e.g. IP address,
   *        filter state, etc.).
   * @return absl::optional<uint64_t> an optional hash value to route on. A hash value might not be
   * returned if the hash policy implementation doesn't find the expected data in the connection
   * (e.g. IP address is null, filter state is not populated, etc.).
   */
  virtual absl::optional<uint64_t> generateHash(const Network::Connection& connection) const PURE;
};
} // namespace Network
} // namespace Envoy
