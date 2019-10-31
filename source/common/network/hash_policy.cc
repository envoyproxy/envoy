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
  hash_impls_.reserve(hash_policies.size());

  for (auto* hash_policy : hash_policies) {
    switch (hash_policy->policy_specifier_case()) {
    case envoy::type::HashPolicy::kSourceIp:
      hash_impls_.emplace_back(new SourceIpHashMethod());
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
    const absl::optional<uint64_t> new_hash = hash_impl->evaluate(downstream_addr, upstream_addr);
    if (new_hash) {
      // Rotating the old value prevents duplicate hash rules from cancelling each other out
      // and preserves all of the entropy
      const uint64_t old_value = hash ? ((hash.value() << 1) | (hash.value() >> 63)) : 0;
      hash = old_value ^ new_hash.value();
    }
  }
  return hash;
}

} // namespace Network
} // namespace Envoy
