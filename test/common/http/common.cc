#include "common.h"

#include <string>

#include "envoy/http/header_map.h"

namespace Envoy {
void HttpTestUtility::addDefaultHeaders(Http::HeaderMap& headers) {
  headers.insertScheme().value(std::string("http"));
  headers.insertMethod().value(std::string("GET"));
  headers.insertHost().value(std::string("host"));
  headers.insertPath().value(std::string("/"));
}
} // namespace Envoy
