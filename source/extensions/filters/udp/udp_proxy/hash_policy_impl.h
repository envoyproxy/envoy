#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/udp/hash_policy.h"

#include "source/common/common/hash.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

using namespace envoy::extensions::filters::udp::udp_proxy::v3;

/**
 * Implementation of HashPolicy that reads from the UDP proxy filter config.
 */
class HashPolicyImpl : public Udp::HashPolicy {
public:
  explicit HashPolicyImpl(const absl::Span<const UdpProxyConfig::HashPolicy* const>& hash_policies);

  // Udp::HashPolicy
  absl::optional<uint64_t>
  generateHash(const Network::Address::Instance& downstream_addr) const override;

  class HashMethod {
  public:
    virtual ~HashMethod() = default;
    virtual absl::optional<uint64_t>
    evaluate(const Network::Address::Instance& downstream_addr) const PURE;
  };

  using HashMethodPtr = std::unique_ptr<HashMethod>;

private:
  HashMethodPtr hash_impl_;
};

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
