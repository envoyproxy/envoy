#include "common.h"

#include <string>

#include "envoy/http/header_map.h"

namespace Envoy {
void HttpTestUtility::addDefaultHeaders(Http::RequestHeaderMap& headers,
                                        const std::string default_method) {
  headers.setReferenceKey(Http::Headers::get().Scheme, Http::Headers::get().SchemeValues.Http);
  headers.setMethod(default_method);
  headers.setHost("host");
  headers.setPath("/");
}
} // namespace Envoy
