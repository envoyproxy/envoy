#pragma once

#include <string>

#include "envoy/http/header_map.h"

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

class ZipkinCoreConstantValues {
public:
  const std::string CLIENT_SEND = "cs";
  const std::string CLIENT_RECV = "cr";
  const std::string SERVER_SEND = "ss";
  const std::string SERVER_RECV = "sr";

  const std::string HTTP_HOST = "http.host";
  const std::string HTTP_METHOD = "http.method";
  const std::string HTTP_PATH = "http.path";
  const std::string HTTP_URL = "http.url";
  const std::string HTTP_STATUS_CODE = "http.status_code";
  const std::string HTTP_REQUEST_SIZE = "http.request.size";
  const std::string HTTP_RESPONSE_SIZE = "http.response.size";

  const std::string LOCAL_COMPONENT = "lc";
  const std::string ERROR = "error";
  const std::string CLIENT_ADDR = "ca";
  const std::string SERVER_ADDR = "sa";

  // Zipkin B3 headers
  const Http::LowerCaseString X_B3_TRACE_ID{"x-b3-traceid"};
  const Http::LowerCaseString X_B3_SPAN_ID{"x-b3-spanid"};
  const Http::LowerCaseString X_B3_PARENT_SPAN_ID{"x-b3-parentspanid"};
  const Http::LowerCaseString X_B3_SAMPLED{"x-b3-sampled"};
  const Http::LowerCaseString X_B3_FLAGS{"x-b3-flags"};

  // Zipkin b3 single header
  const Http::LowerCaseString B3{"b3"};

  const std::string SAMPLED = "1";
  const std::string NOT_SAMPLED = "0";

  const std::string DEFAULT_COLLECTOR_ENDPOINT = "/api/v1/spans";
  const bool DEFAULT_SHARED_SPAN_CONTEXT = true;
};

typedef ConstSingleton<ZipkinCoreConstantValues> ZipkinCoreConstants;

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
