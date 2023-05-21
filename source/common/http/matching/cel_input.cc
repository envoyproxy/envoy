#include "source/common/http/matching/cel_input.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Http {
namespace Matching {

REGISTER_FACTORY(HttpCelDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);

} // namespace Matching
} // namespace Http
} // namespace Envoy
