#include "source/extensions/path/rewrite/uri_template/config.h"

#include "envoy/registry/registry.h"
#include "envoy/router/path_rewriter.h"

namespace Envoy {
namespace Extensions {
namespace UriTemplate {
namespace Rewrite {

REGISTER_FACTORY(UriTemplateRewriterFactory, Router::PathRewriterFactory);

} // namespace Rewrite
} // namespace UriTemplate
} // namespace Extensions
} // namespace Envoy
