#include "source/extensions/path/match/pattern_template/config.h"

#include "envoy/registry/registry.h"
#include "envoy/router/path_match_policy.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Match {

REGISTER_FACTORY(PatternTemplateMatchPredicateFactory, Router::PathMatchPredicateFactory);

} // namespace Match
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
