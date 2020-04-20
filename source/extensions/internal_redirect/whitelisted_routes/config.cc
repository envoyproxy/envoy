#include "extensions/internal_redirect/whitelisted_routes/config.h"

#include "envoy/registry/registry.h"
#include "envoy/router/internal_redirect.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

REGISTER_FACTORY(WhitelistedRoutesPredicateFactory, Router::InternalRedirectPredicateFactory);

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
