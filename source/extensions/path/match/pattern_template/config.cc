#include "source/extensions/path/match/pattern_template/config.h"

#include "envoy/registry/registry.h"
#include "envoy/router/path_matcher.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {
namespace Match {

REGISTER_FACTORY(PatternTemplateMatcherFactory, Router::PathMatcherFactory);

} // namespace Match
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
