#include "common/router/tls_context_match_criteria_impl.h"

#include "envoy/api/v3alpha/route/route.pb.h"

namespace Envoy {
namespace Router {

TlsContextMatchCriteriaImpl::TlsContextMatchCriteriaImpl(
    const envoy::api::v3alpha::route::RouteMatch::TlsContextMatchOptions& options) {
  if (options.has_presented()) {
    presented_ = options.presented().value();
  }
}

} // namespace Router
} // namespace Envoy
