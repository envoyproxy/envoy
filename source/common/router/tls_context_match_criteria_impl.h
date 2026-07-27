#pragma once

#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Router {

class TlsContextMatchCriteriaImpl : public TlsContextMatchCriteria {
public:
  TlsContextMatchCriteriaImpl(
      const envoy::config::route::v3::RouteMatch::TlsContextMatchOptions& options);

  const std::optional<bool>& presented() const override { return presented_; }
  const std::optional<bool>& validated() const override { return validated_; }

private:
  std::optional<bool> presented_;
  std::optional<bool> validated_;
};

} // namespace Router
} // namespace Envoy
