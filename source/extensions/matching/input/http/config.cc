#include "extensions/matching/input/http/config.h"

namespace Envoy {

REGISTER_FACTORY(HttpResponseHeadersFactory, DataInputFactory<Http::HttpMatchingData>);

REGISTER_FACTORY(HttpRequestHeadersFactory, DataInputFactory<Http::HttpMatchingData>);

REGISTER_FACTORY(FixedDataInputFactory,
                 DataInputFactory<Http::HttpMatchingData>);
}
