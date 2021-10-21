#include "source/common/network/hash_policy.h"

#include "envoy/common/exception.h"
#include "envoy/common/hashable.h"
#include "envoy/type/v3/hash_policy.pb.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Network {

class SourceIpHashMethod : public HashPolicyImpl::HashMethod {
public:
  absl::optional<uint64_t> evaluate(const Network::Address::Instance* downstream_addr,
                                    const Network::Address::Instance*,
                                    const StreamInfo::FilterState&) const override {
    if (downstream_addr && downstream_addr->ip()) {
      ASSERT(!downstream_addr->ip()->addressAsString().empty());
      return HashUtil::xxHash64(downstream_addr->ip()->addressAsString());
    }

    return absl::nullopt;
  }
};

class FilterStateHashMethod : public HashPolicyImpl::HashMethod {
public:
  FilterStateHashMethod(absl::string_view key) : key_(key) {}

  absl::optional<uint64_t> evaluate(const Network::Address::Instance*,
                                    const Network::Address::Instance*,
                                    const StreamInfo::FilterState& filter_state) const override {
    if (filter_state.hasData<Hashable>(key_)) {
      return filter_state.getDataReadOnly<Hashable>(key_).hash();
    }
    return absl::nullopt;
  }

private:
  const std::string key_;
};

HashPolicyImpl::HashPolicyImpl(
    const absl::Span<const envoy::type::v3::HashPolicy* const>& hash_policies) {
  ASSERT(hash_policies.size() == 1);
  switch (hash_policies[0]->policy_specifier_case()) {
  case envoy::type::v3::HashPolicy::PolicySpecifierCase::kSourceIp:
    hash_impl_ = std::make_unique<SourceIpHashMethod>();
    break;
  case envoy::type::v3::HashPolicy::PolicySpecifierCase::kFilterState:
    hash_impl_ = std::make_unique<FilterStateHashMethod>(hash_policies[0]->filter_state().key());
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

absl::optional<uint64_t>
HashPolicyImpl::generateHash(const Network::Address::Instance* downstream_addr,
                             const Network::Address::Instance* upstream_addr,
                             const StreamInfo::FilterState& filter_state) const {
  return hash_impl_->evaluate(downstream_addr, upstream_addr, filter_state);
}

} // namespace Network
} // namespace Envoy
