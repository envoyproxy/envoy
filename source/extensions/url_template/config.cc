#include "source/extensions/url_template/config.h"

#include "envoy/registry/registry.h"
#include "envoy/router/url_template.h"
// TODO change includes

namespace Envoy {
namespace Extensions {
namespace UrlTemplate {

REGISTER_FACTORY(UrlTemplatePredicateFactory, Router::UrlTemplatePredicateFactory);

} // namespace UrlTemplate
} // namespace Extensions
} // namespace Envoy