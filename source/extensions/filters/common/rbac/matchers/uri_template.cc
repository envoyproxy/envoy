#include "uri_template.h"

#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/path/match/uri_template/uri_template_match.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace Matchers {

using namespace Filters::Common::RBAC;

bool UriTemplateMatcher::matches(const Network::Connection&,
                                  const Envoy::Http::RequestHeaderMap& headers,
                                  const StreamInfo::StreamInfo&) const {
  return uri_template_matcher_.match(headers.getPathValue());
}

REGISTER_FACTORY(UriTemplateMatcherFactory, MatcherExtensionFactory);

} // namespace Matchers
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
