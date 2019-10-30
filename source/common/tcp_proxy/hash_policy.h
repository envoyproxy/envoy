#pragma once

#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.h"
#include "envoy/network/hash_policy.h"

#include "common/common/hash.h"

namespace Envoy {
namespace TcpProxy {
/**
 * Implementation of HashPolicy that reads from the proto TCP proxy config.
 */
class HashPolicyImpl : public Network::HashPolicy {
public:
  explicit HashPolicyImpl(
      absl::Span<const envoy::config::filter::network::tcp_proxy::v2::TcpProxy::HashPolicy* const>
          hash_policy);

  // Network::HashPolicy
  absl::optional<uint64_t>
  generateHash(const Network::Address::Instance* downstream_addr,
               const Network::Address::Instance* upstream_addr) const override;

  class HashMethod {
  public:
    virtual ~HashMethod() = default;
    virtual absl::optional<uint64_t>
    evaluate(const Network::Address::Instance* downstream_addr,
             const Network::Address::Instance* upstream_addr) const PURE;
  };

  using HashMethodPtr = std::unique_ptr<HashMethod>;

private:
  std::vector<HashMethodPtr> hash_impls_;
};
} // namespace TcpProxy
} // namespace Envoy