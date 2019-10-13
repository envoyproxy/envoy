#pragma once

#include "envoy/api/v2/route/route.pb.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

class TlsContextMatchCriteriaImpl;
using TlsContextMatchCriteriaImplConstPtr = std::unique_ptr<const TlsContextMatchCriteriaImpl>;

class TlsContextMatchCriteriaImpl : public TlsContextMatchCriteria {
public:
  TlsContextMatchCriteriaImpl(
      const ::envoy::api::v2::route::RouteMatch_TlsContextMatchOptions& options);

  const absl::optional<bool>& presented() const override { return presented_; }

  const absl::optional<bool>& expired() const override { return expired_; }

private:
  absl::optional<bool> presented_;
  absl::optional<bool> expired_;
};

} // namespace Router
} // namespace Envoy
