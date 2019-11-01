#include "common/network/hash_policy.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Network {

class SourceIpHashMethod : public HashPolicyImpl::HashMethod {
public:
  absl::optional<uint64_t> evaluate(const Network::Address::Instance* downstream_addr,
                                    const Network::Address::Instance*) const override {
    if (downstream_addr && downstream_addr->ip() &&
        !downstream_addr->ip()->addressAsString().empty()) {
      return HashUtil::xxHash64(downstream_addr->ip()->addressAsString());
    }

    return absl::nullopt;
  }
};

HashPolicyImpl::HashPolicyImpl(absl::Span<const envoy::type::HashPolicy* const> hash_policies) {
  ASSERT(hash_policies.size() == 1);
  for (auto* hash_policy : hash_policies) {
    switch (hash_policy->policy_specifier_case()) {
    case envoy::type::HashPolicy::kSourceIp:
      hash_impl_ = std::make_unique<SourceIpHashMethod>();
      break;
    default:
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }
  }
}

absl::optional<uint64_t>
HashPolicyImpl::generateHash(const Network::Address::Instance* downstream_addr,
                             const Network::Address::Instance* upstream_addr) const {
  absl::optional<uint64_t> hash;
  for (const HashMethodPtr& hash_impl : hash_impls_) {
    // Only one hash policy is allowed. Modify this code when enabling multiple hash policies.
    hash = hash_impl->evaluate(downstream_addr, upstream_addr);
  }
  return hash;
}

} // namespace Network
} // namespace Envoy
