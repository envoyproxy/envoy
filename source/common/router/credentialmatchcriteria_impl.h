#pragma once

#include "envoy/api/v2/route/route.pb.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

class CredentialMatchCriteriaImpl;
using CredentialMatchCriteriaImplConstPtr = std::unique_ptr<const CredentialMatchCriteriaImpl>;

class CredentialMatchCriteriaImpl : public CredentialMatchCriteria {
public:
  CredentialMatchCriteriaImpl(
      const ::envoy::api::v2::route::RouteMatch_CredentialMatchOptions& options);

  const absl::optional<bool>& presented() const override { return presented_; }

  const absl::optional<bool>& expired() const override { return expired_; }

private:
  absl::optional<bool> presented_;
  absl::optional<bool> expired_;
};

} // namespace Router
} // namespace Envoy
