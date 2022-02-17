#include "common.h"

#include <string>

#include "envoy/http/header_map.h"

namespace Envoy {
void HttpTestUtility::addDefaultHeaders(Http::RequestHeaderMap& headers, bool overwrite) {
  if (overwrite || headers.getSchemeValue().empty()) {
    headers.setScheme("http");
  }
  if (overwrite || headers.getMethodValue().empty()) {
    headers.setMethod("GET");
  }
  if (overwrite || headers.getHostValue().empty()) {
    headers.setHost("host");
  }
  if (overwrite || headers.getPathValue().empty()) {
    headers.setPath("/");
  }
}
} // namespace Envoy
