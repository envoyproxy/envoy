#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"
#include "test/integration/filters/common.h"

namespace Envoy {

// Registers the passthrough filter for use in test.
class TestPassThroughFilter : public Http::PassThroughFilter {
public:
  constexpr static char name[] = "passthrough-filter";
};

constexpr char TestPassThroughFilter::name[];
static Registry::RegisterFactory<SimpleFilterConfig<TestPassThroughFilter>,
                                 Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace Envoy
