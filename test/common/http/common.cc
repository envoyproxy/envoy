#include "common.h"

#include "envoy/http/header_map.h"

void HttpTestUtility::addDefaultHeaders(Http::HeaderMap& headers) {
  headers.addViaCopy(":scheme", "http");
  headers.addViaCopy(":method", "GET");
  headers.addViaCopy(":authority", "host");
  headers.addViaCopy(":path", "/");
}
