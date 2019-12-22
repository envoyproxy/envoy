#pragma once

#include "envoy/api/v3alpha/route/route.pb.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

class TlsContextMatchCriteriaImpl : public TlsContextMatchCriteria {
public:
  TlsContextMatchCriteriaImpl(
      const envoy::api::v3alpha::route::RouteMatch::TlsContextMatchOptions& options);

  const absl::optional<bool>& presented() const override { return presented_; }

private:
  absl::optional<bool> presented_;
};

} // namespace Router
} // namespace Envoy
