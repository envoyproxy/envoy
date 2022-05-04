#include "source/extensions/filters/udp/udp_proxy/hash_policy_impl.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"

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
  explicit KeyHashMethod(const std::string& key) : hash_{HashUtil::xxHash64(key)} {
    ASSERT(!key.empty());
  }

  absl::optional<uint64_t>
  evaluate(const Network::Address::Instance& downstream_addr) const override {
    UNREFERENCED_PARAMETER(downstream_addr);
    return hash_;
  }

private:
  const uint64_t hash_;
};

absl::optional<uint64_t>
HashPolicyImpl::generateHash(const Network::Address::Instance& downstream_addr) const {
  return hash_impl_->evaluate(downstream_addr);
}

HashPolicyImplPtr HashPolicyImplFactory::create(
    const Protobuf::RepeatedPtrField<
        envoy::extensions::filters::udp::udp_proxy::v3::UdpProxyConfig_HashPolicy>& hash_policies) {
  ASSERT(hash_policies.size() == 1);
  switch (hash_policies[0].policy_specifier_case()) {
  case UdpProxyConfig::HashPolicy::PolicySpecifierCase::kSourceIp:
    return std::make_unique<HashPolicyImpl>(std::make_unique<SourceIpHashMethod>());
    break;
  case UdpProxyConfig::HashPolicy::PolicySpecifierCase::kKey:
    return std::make_unique<HashPolicyImpl>(
        std::make_unique<KeyHashMethod>(hash_policies[0].key()));
    break;
  case UdpProxyConfig::HashPolicy::PolicySpecifierCase::POLICY_SPECIFIER_NOT_SET:
    throw EnvoyException("hash policy specifier not set");
  }
  throw EnvoyException(absl::StrCat("unexpected hash policy specifier case: ",
                                    hash_policies[0].policy_specifier_case()));
}

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
