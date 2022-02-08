#pragma once

#include "envoy/network/connection.h"
#include "envoy/network/hash_policy.h"
#include "envoy/type/v3/hash_policy.pb.h"

#include "source/common/common/hash.h"

namespace Envoy {
namespace Network {
/**
 * Implementation of HashPolicy that reads from the proto TCP proxy config.
 */
class HashPolicyImpl : public Network::HashPolicy {
public:
  explicit HashPolicyImpl(const absl::Span<const envoy::type::v3::HashPolicy* const>& hash_policy);

  // Network::HashPolicy
  absl::optional<uint64_t> generateHash(const Network::Connection& connection) const override;

  class HashMethod {
  public:
    virtual ~HashMethod() = default;
    virtual absl::optional<uint64_t> evaluate(const Network::Connection& connection) const PURE;
  };

  using HashMethodPtr = std::unique_ptr<HashMethod>;

private:
  HashMethodPtr hash_impl_;
};
} // namespace Network
} // namespace Envoy
