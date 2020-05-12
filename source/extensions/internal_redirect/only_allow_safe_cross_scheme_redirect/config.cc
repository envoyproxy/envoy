#include "extensions/internal_redirect/only_allow_safe_cross_scheme_redirect/config.h"

#include "envoy/registry/registry.h"
#include "envoy/router/internal_redirect.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

REGISTER_FACTORY(OnlyAllowSafeCrossSchemeRedirectPredicateFactory,
                 Router::InternalRedirectPredicateFactory);

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
