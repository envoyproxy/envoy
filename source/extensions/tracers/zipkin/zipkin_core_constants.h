#pragma once

#include <string>

#include "envoy/http/header_map.h"

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

namespace {

constexpr char KIND_CLIENT[] = "CLIENT";
constexpr char KIND_SERVER[] = "SERVER";

constexpr char CLIENT_SEND[] = "cs";
constexpr char CLIENT_RECV[] = "cr";
constexpr char SERVER_SEND[] = "ss";
constexpr char SERVER_RECV[] = "sr";

constexpr char HTTP_HOST[] = "http.host";
constexpr char HTTP_METHOD[] = "http.method";
constexpr char HTTP_PATH[] = "http.path";
constexpr char HTTP_URL[] = "http.url";
constexpr char HTTP_STATUS_CODE[] = "http.status_code";
constexpr char HTTP_REQUEST_SIZE[] = "http.request.size";
constexpr char HTTP_RESPONSE_SIZE[] = "http.response.size";

constexpr char LOCAL_COMPONENT[] = "lc";
constexpr char ERROR[] = "error";
constexpr char CLIENT_ADDR[] = "ca";
constexpr char SERVER_ADDR[] = "sa";

constexpr char SAMPLED[] = "1";
constexpr char NOT_SAMPLED[] = "0";

constexpr char DEFAULT_COLLECTOR_ENDPOINT[] = "/api/v1/spans";
constexpr bool DEFAULT_SHARED_SPAN_CONTEXT = true;

} // namespace

class ZipkinCoreConstantValues {
public:
  // Zipkin B3 headers
  const Http::LowerCaseString X_B3_TRACE_ID{"x-b3-traceid"};
  const Http::LowerCaseString X_B3_SPAN_ID{"x-b3-spanid"};
  const Http::LowerCaseString X_B3_PARENT_SPAN_ID{"x-b3-parentspanid"};
  const Http::LowerCaseString X_B3_SAMPLED{"x-b3-sampled"};
  const Http::LowerCaseString X_B3_FLAGS{"x-b3-flags"};

  // Zipkin b3 single header
  const Http::LowerCaseString B3{"b3"};
};

using ZipkinCoreConstants = ConstSingleton<ZipkinCoreConstantValues>;

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
