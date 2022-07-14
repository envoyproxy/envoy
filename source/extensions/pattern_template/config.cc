#include "source/extensions/pattern_template/config.h"

#include "envoy/registry/registry.h"
#include "envoy/router/pattern_template.h"

namespace Envoy {
namespace PatternTemplate {

REGISTER_FACTORY(PatternTemplatePredicateFactory, Router::PatternTemplatePredicateFactory);
} // namespace matching
} // namespace Envoy