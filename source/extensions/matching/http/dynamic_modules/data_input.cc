#include "source/extensions/matching/http/dynamic_modules/data_input.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Http {
namespace DynamicModules {

REGISTER_FACTORY(HttpDynamicModuleDataInputFactory,
                 ::Envoy::Matcher::DataInputFactory<::Envoy::Http::HttpMatchingData>);

} // namespace DynamicModules
} // namespace Http
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
