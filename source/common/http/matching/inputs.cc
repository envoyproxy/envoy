#include "source/common/http/matching/inputs.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Http {
namespace Matching {

using HttpRequestHeadersDataInputFactory = HttpRequestHeadersDataInputFactoryBase<HttpMatchingData>;
using HttpResponseHeadersDataInputFactory =
    HttpResponseHeadersDataInputFactoryBase<HttpMatchingData>;
using HttpRequestTrailersDataInputFactory =
    HttpRequestTrailersDataInputFactoryBase<HttpMatchingData>;
using HttpResponseTrailersDataInputFactory =
    HttpResponseTrailersDataInputFactoryBase<HttpMatchingData>;

REGISTER_FACTORY(HttpRequestHeadersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpResponseHeadersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpRequestTrailersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);
REGISTER_FACTORY(HttpResponseTrailersDataInputFactory, Matcher::DataInputFactory<HttpMatchingData>);

} // namespace Matching
} // namespace Http
} // namespace Envoy
