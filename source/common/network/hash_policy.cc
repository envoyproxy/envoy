#include "source/common/network/hash_policy.h"

#include "envoy/common/exception.h"
#include "envoy/common/hashable.h"
#include "envoy/type/v3/hash_policy.pb.h"

#include "source/common/common/assert.h"

namespace Envoy {
namespace Network {

class SourceIpHashMethod : public HashPolicyImpl::HashMethod {
public:
  absl::optional<uint64_t> evaluate(const Network::Connection& connection) const override {
    const auto* downstream_addr = connection.connectionInfoProvider().remoteAddress().get();
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

  absl::optional<uint64_t> evaluate(const Network::Connection& connection) const override {
    const auto& filter_state = connection.streamInfo().filterState();
    if (auto typed_state = filter_state.getDataReadOnly<Hashable>(key_); typed_state != nullptr) {
      return typed_state->hash();
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
  case envoy::type::v3::HashPolicy::PolicySpecifierCase::POLICY_SPECIFIER_NOT_SET:
    PANIC_DUE_TO_PROTO_UNSET;
  }
}

absl::optional<uint64_t> HashPolicyImpl::generateHash(const Network::Connection& connection) const {
  return hash_impl_->evaluate(connection);
}

} // namespace Network
} // namespace Envoy
