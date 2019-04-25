#include "common.h"

#include <string>

#include "envoy/http/header_map.h"

namespace Envoy {
void HttpTestUtility::addDefaultHeaders(Http::HeaderMap& headers,
                                        const std::string default_method) {
  headers.insertScheme().value(std::string("http"));
  headers.insertMethod().value(default_method);
  headers.insertHost().value(std::string("host"));
  headers.insertPath().value(std::string("/"));
}
} // namespace Envoy
