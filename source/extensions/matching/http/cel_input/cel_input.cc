#include "source/extensions/matching/http/cel_input/cel_input.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Http {
namespace CelInput {

REGISTER_FACTORY(HttpCelDataInputFactory,
                 Matcher::DataInputFactory<::Envoy::Http::HttpMatchingData>);

} // namespace CelInput
} // namespace Http
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
