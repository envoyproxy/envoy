#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/udp/hash_policy.h"

#include "source/common/common/hash.h"
#include "source/common/protobuf/protobuf.h"

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
  class HashMethod {
  public:
    virtual ~HashMethod() = default;
    virtual absl::optional<uint64_t>
    evaluate(const Network::Address::Instance& downstream_addr) const PURE;
  };

  using HashMethodPtr = std::unique_ptr<HashMethod>;
  using HashPolicyImplPtr = std::unique_ptr<const HashPolicyImpl>;

  static HashPolicyImplPtr
  create(const Protobuf::RepeatedPtrField<
         envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig_HashPolicy>& hash_policies);

  absl::optional<uint64_t>
  generateHash(const Network::Address::Instance& downstream_addr) const override;

private:
  HashPolicyImpl(HashMethodPtr hash_impl) : hash_impl_(std::move(hash_impl)) {}
  HashMethodPtr hash_impl_;
};

using HashPolicyImplPtr = std::unique_ptr<const HashPolicyImpl>;

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
