#include "source/extensions/path/rewrite/pattern_template/config.h"

#include "envoy/registry/registry.h"
#include "envoy/router/path_rewrite.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {
namespace Rewrite {

REGISTER_FACTORY(PatternTemplateRewriterFactory, Router::PathRewriterFactory);

} // namespace Rewrite
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
