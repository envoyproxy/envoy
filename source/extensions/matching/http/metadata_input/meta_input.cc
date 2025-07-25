#include "source/extensions/matching/http/metadata_input/meta_input.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Http {
namespace MetadataInput {

class HttpDynamicMetadataInputFactory
    : public DynamicMetadataInputBaseFactory<Envoy::Http::HttpMatchingData> {};
REGISTER_FACTORY(HttpDynamicMetadataInputFactory,
                 Matcher::DataInputFactory<Envoy::Http::HttpMatchingData>);

} // namespace MetadataInput
} // namespace Http
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
