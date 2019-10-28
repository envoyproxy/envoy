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
  const absl::optional<bool>& validated() const override { return validated_; }

private:
  absl::optional<bool> presented_;
  absl::optional<bool> validated_;
};

} // namespace Router
} // namespace Envoy
