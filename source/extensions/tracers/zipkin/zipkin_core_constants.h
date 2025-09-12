#pragma once

#include <string>

#include "envoy/http/header_map.h"

#include "source/common/singleton/const_singleton.h"
#include "source/common/tracing/trace_context_impl.h"
#include "source/extensions/propagators/b3/multi/b3_multi_propagator.h"
#include "source/extensions/propagators/w3c/tracecontext/tracecontext_propagator.h"

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

constexpr bool DEFAULT_SHARED_SPAN_CONTEXT = true;

} // namespace
} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
