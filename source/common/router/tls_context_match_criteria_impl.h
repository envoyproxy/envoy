#pragma once

#include "envoy/api/v2/route/route.pb.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

class TlsContextMatchCriteriaImpl : public TlsContextMatchCriteria {
public:
  TlsContextMatchCriteriaImpl(
      const ::envoy::api::v2::route::RouteMatch_TlsContextMatchOptions& options);

  const absl::optional<bool>& presented() const override { return presented_; }

private:
  absl::optional<bool> presented_;
};

} // namespace Router
} // namespace Envoy
