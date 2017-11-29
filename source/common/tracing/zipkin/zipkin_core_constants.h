#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
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
  const std::string X_B3_TRACE_ID = "X-B3-TraceId";
  const std::string X_B3_SPAN_ID = "X-B3-SpanId";
  const std::string X_B3_PARENT_SPAN_ID = "X-B3-ParentSpanId";
  const std::string X_B3_SAMPLED = "X-B3-Sampled";
  const std::string X_B3_FLAGS = "X-B3-Flags";

  const std::string ALWAYS_SAMPLE = "1";

  const std::string DEFAULT_COLLECTOR_ENDPOINT = "/api/v1/spans";
};

typedef ConstSingleton<ZipkinCoreConstantValues> ZipkinCoreConstants;

} // namespace Zipkin
} // namespace Envoy
