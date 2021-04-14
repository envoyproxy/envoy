#include "extensions/filters/udp/udp_proxy/hash_policy_impl.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

class SourceIpHashMethod : public HashPolicyImpl::HashMethod {
public:
  absl::optional<uint64_t>
  evaluate(const Network::Address::Instance& downstream_addr) const override {
    if (downstream_addr.ip()) {
      ASSERT(!downstream_addr.ip()->addressAsString().empty());
      return HashUtil::xxHash64(downstream_addr.ip()->addressAsString());
    }

    return absl::nullopt;
  }
};

class KeyHashMethod : public HashPolicyImpl::HashMethod {
public:
  KeyHashMethod(const std::string& key) : Key(key) {}
  const std::string& Key;

  absl::optional<uint64_t>
  evaluate(const Network::Address::Instance& downstream_addr) const override {
    (void)downstream_addr;
    // ASSERT(!Key.empty());
    if (!Key.empty()) {
      uint64_t hash = HashUtil::xxHash64(Key);
      return hash;
    }

    return absl::nullopt;
  }
};

HashPolicyImpl::HashPolicyImpl(
    const absl::Span<const UdpProxyConfig::HashPolicy* const>& hash_policies) {
  ASSERT(hash_policies.size() == 1);
  switch (hash_policies[0]->policy_specifier_case()) {
  case UdpProxyConfig::HashPolicy::PolicySpecifierCase::kSourceIp:
    hash_impl_ = std::make_unique<SourceIpHashMethod>();
    break;
  case UdpProxyConfig::HashPolicy::PolicySpecifierCase::kKey:
    hash_impl_ = std::make_unique<KeyHashMethod>(hash_policies[0]->key());
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

absl::optional<uint64_t>
HashPolicyImpl::generateHash(const Network::Address::Instance& downstream_addr) const {
  return hash_impl_->evaluate(downstream_addr);
}

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
