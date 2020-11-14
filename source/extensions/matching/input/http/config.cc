#include "extensions/matching/input/http/config.h"

namespace Envoy {

REGISTER_FACTORY(HttpResponseBodyFactory, DataInputFactory<Http::HttpMatchingData>);

REGISTER_FACTORY(HttpRequestBodyFactory, DataInputFactory<Http::HttpMatchingData>);

REGISTER_FACTORY(HttpResponseHeadersFactory, DataInputFactory<Http::HttpMatchingData>);

REGISTER_FACTORY(HttpRequestHeadersFactory, DataInputFactory<Http::HttpMatchingData>);

REGISTER_FACTORY(HttpResponseTrailersFactory, DataInputFactory<Http::HttpMatchingData>);

REGISTER_FACTORY(HttpRequestTrailersFactory, DataInputFactory<Http::HttpMatchingData>);

REGISTER_FACTORY(FixedDataInputFactory, DataInputFactory<Http::HttpMatchingData>);
} // namespace Envoy
