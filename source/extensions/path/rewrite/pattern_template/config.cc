#include "source/extensions/path/rewrite/pattern_template/config.h"

#include "envoy/registry/registry.h"
#include "envoy/router/path_rewrite_policy.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Rewrite {

REGISTER_FACTORY(PatternTemplateRewritePredicateFactory, Router::PathRewritePredicateFactory);
} // namespace Rewrite
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
