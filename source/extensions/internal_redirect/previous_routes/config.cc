#include "source/extensions/internal_redirect/previous_routes/config.h"

#include "envoy/registry/registry.h"
#include "envoy/router/internal_redirect.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

REGISTER_FACTORY(PreviousRoutesPredicateFactory, Router::InternalRedirectPredicateFactory);

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
