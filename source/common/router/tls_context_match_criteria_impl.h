#pragma once

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

class TlsContextMatchCriteriaImpl : public TlsContextMatchCriteria {
public:
  TlsContextMatchCriteriaImpl(
      const envoy::config::route::v3::RouteMatch::TlsContextMatchOptions& options);

  const absl::optional<bool>& presented() const override { return presented_; }

private:
  absl::optional<bool> presented_;
};

} // namespace Router
} // namespace Envoy
