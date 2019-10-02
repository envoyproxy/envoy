#include "common/router/credentialmatchcriteria_impl.h"

namespace Envoy {
namespace Router {

CredentialMatchCriteriaImpl::CredentialMatchCriteriaImpl(
  const ::envoy::api::v2::route::RouteMatch_CredentialMatchOptions& options) {

  if (options.has_presented()) {
    presented_ = options.presented().value();
  }

  if (options.has_expired()) {
    expired_ = options.expired().value();
  }
}

} // namespace Router
} // namespace Envoy
