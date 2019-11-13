#include "common.h"

#include <string>

#include "envoy/http/header_map.h"

namespace Envoy {
void HttpTestUtility::addDefaultHeaders(Http::HeaderMap& headers,
                                        const std::string default_method) {
  headers.setScheme("http");
  headers.setMethod(default_method);
  headers.setHost("host");
  headers.setPath("/");
}
} // namespace Envoy
