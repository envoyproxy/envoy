#pragma once

#include "envoy/network/address.h"
#include "envoy/stream_info/filter_state.h"

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
   * @param filter_state is the per-connection filter state, which may contain Objects that are
   *        Envoy::Hashable. These can be used for generating the hash.
   * @return absl::optional<uint64_t> an optional hash value to route on. A hash value might not be
   * returned if for example the downstream address is nullptr.
   */
  virtual absl::optional<uint64_t>
  generateHash(const Network::Address::Instance* downstream_address,
               const Network::Address::Instance* upstream_address,
               const StreamInfo::FilterState& filter_state) const PURE;
};
} // namespace Network
} // namespace Envoy
