#include "common/router/tls_context_match_criteria_impl.h"

#include "envoy/config/route/v3/route_components.pb.h"

namespace Envoy {
namespace Router {

TlsContextMatchCriteriaImpl::TlsContextMatchCriteriaImpl(
    const envoy::config::route::v3::RouteMatch::TlsContextMatchOptions& options) {
  if (options.has_presented()) {
    presented_ = options.presented().value();
  }
}

} // namespace Router
} // namespace Envoy
