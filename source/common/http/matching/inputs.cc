#include "source/common/http/matching/inputs.h"

#include "envoy/registry/registry.h"

#include "source/common/http/matching/status_code_input.h"

namespace Envoy {
namespace Http {
namespace Matching {
REGISTER_FACTORY(HttpRequestHeadersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpResponseHeadersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpRequestTrailersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpResponseTrailersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpResponseStatusCodeInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpResponseStatusCodeClassInputFactory,
                 Matcher::DataInputFactory<HttpMatchingData>);
} // namespace Matching
} // namespace Http
} // namespace Envoy
