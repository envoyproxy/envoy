#include "common/network/hash_policy.h"

#include "envoy/common/exception.h"
#include "envoy/type/v3/hash_policy.pb.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Network {

class SourceIpHashMethod : public HashPolicyImpl::HashMethod {
public:
  absl::optional<uint64_t> evaluate(const Network::Address::Instance* downstream_addr,
                                    const Network::Address::Instance*) const override {
    if (downstream_addr && downstream_addr->ip()) {
      ASSERT(!downstream_addr->ip()->addressAsString().empty());
      return HashUtil::xxHash64(downstream_addr->ip()->addressAsString());
    }

    return absl::nullopt;
  }
};

HashPolicyImpl::HashPolicyImpl(
    const absl::Span<const envoy::type::v3::HashPolicy* const>& hash_policies) {
  ASSERT(hash_policies.size() == 1);
  switch (hash_policies[0]->policy_specifier_case()) {
  case envoy::type::v3::HashPolicy::PolicySpecifierCase::kSourceIp:
    hash_impl_ = std::make_unique<SourceIpHashMethod>();
    break;
  default:
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
}

absl::optional<uint64_t>
HashPolicyImpl::generateHash(const Network::Address::Instance* downstream_addr,
                             const Network::Address::Instance* upstream_addr) const {
  return hash_impl_->evaluate(downstream_addr, upstream_addr);
}

} // namespace Network
} // namespace Envoy
