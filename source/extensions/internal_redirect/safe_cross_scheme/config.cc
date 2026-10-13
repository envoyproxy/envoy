#include "source/extensions/internal_redirect/safe_cross_scheme/config.h"

#include "envoy/registry/registry.h"
#include "envoy/router/internal_redirect.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

REGISTER_FACTORY(SafeCrossSchemePredicateFactory, Router::InternalRedirectPredicateFactory);

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
