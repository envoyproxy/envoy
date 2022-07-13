#include "source/extensions/url_template/config.h"

#include "envoy/registry/registry.h"
#include "envoy/router/url_template.h"

namespace Envoy {
namespace matching {

REGISTER_FACTORY(PatternTemplatePredicateFactory, Router::PatternTemplatePredicateFactory);
} // namespace matching
} // namespace Envoy