#include "common/router/tlscontextmatchcriteria_impl.h"

namespace Envoy {
namespace Router {

TlsContextMatchCriteriaImpl::TlsContextMatchCriteriaImpl(
    const ::envoy::api::v2::route::RouteMatch_TlsContextMatchOptions& options) {

  if (options.has_presented()) {
    presented_ = options.presented().value();
  }

  if (options.has_expired()) {
    expired_ = options.expired().value();
  }
}

} // namespace Router
} // namespace Envoy
