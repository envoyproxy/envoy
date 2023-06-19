#include "source/common/http/matching/status_code_input.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Http {
namespace Matching {
REGISTER_FACTORY(HttpResponseStatusCodeInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpResponseStatusCodeClassInputFactory,
                 Matcher::DataInputFactory<HttpMatchingData>);

} // namespace Matching
} // namespace Http
} // namespace Envoy
