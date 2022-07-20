#include "source/extensions/pattern_template/match/config.h"

#include "envoy/registry/registry.h"
#include "envoy/router/pattern_template.h"

namespace Envoy {
namespace Extensions {
namespace PatternTemplate {
namespace Match {

REGISTER_FACTORY(PatternTemplateMatchPredicateFactory,
                 Router::PatternTemplateMatchPredicateFactory);
} // namespace Match
} // namespace PatternTemplate
} // namespace Extensions
} // namespace Envoy
