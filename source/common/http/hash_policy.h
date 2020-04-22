#pragma once

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/http/hash_policy.h"
#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Http {

/**
 * Implementation of HashPolicy that reads from the proto route config and only currently supports
 * hashing on an HTTP header.
 */
class HashPolicyImpl : public HashPolicy {
public:
  explicit HashPolicyImpl(
      absl::Span<const envoy::config::route::v3::RouteAction::HashPolicy* const> hash_policy);

  // Http::HashPolicy
  absl::optional<uint64_t>
  generateHash(const Network::Address::Instance* downstream_addr, const RequestHeaderMap& headers,
               const AddCookieCallback add_cookie,
               const StreamInfo::FilterStateSharedPtr filter_state) const override;

  class HashMethod {
  public:
    virtual ~HashMethod() = default;
    virtual absl::optional<uint64_t>
    evaluate(const Network::Address::Instance* downstream_addr, const RequestHeaderMap& headers,
             const AddCookieCallback add_cookie,
             const StreamInfo::FilterStateSharedPtr filter_state) const PURE;

    // If the method is a terminal method, ignore rest of the hash policy chain.
    virtual bool terminal() const PURE;
  };

  using HashMethodPtr = std::unique_ptr<HashMethod>;

private:
  std::vector<HashMethodPtr> hash_impls_;
};

} // namespace Http
} // namespace Envoy
