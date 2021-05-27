#include "extensions/internal_redirect/allow_listed_routes/config.h"

#include "include/envoy/registry/registry.h"
#include "include/envoy/router/internal_redirect.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

REGISTER_FACTORY(AllowListedRoutesPredicateFactory, Router::InternalRedirectPredicateFactory);

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
