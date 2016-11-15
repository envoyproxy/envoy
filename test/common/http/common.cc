#include "common.h"

#include "envoy/http/header_map.h"

void HttpTestUtility::addDefaultHeaders(Http::HeaderMap& headers) {
  headers.insertScheme().value(std::string("http"));
  headers.insertMethod().value(std::string("GET"));
  headers.insertHost().value(std::string("host"));
  headers.insertPath().value(std::string("/"));
}
