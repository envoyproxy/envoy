#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

namespace {

constexpr char SPAN_ID[] = "id";
constexpr char SPAN_KIND[] = "kind";
constexpr char SPAN_NAME[] = "name";
constexpr char SPAN_TAGS[] = "tags";
constexpr char SPAN_SHARED[] = "shared";
constexpr char SPAN_TRACE_ID[] = "traceId";
constexpr char SPAN_DURATION[] = "duration";
constexpr char SPAN_PARENT_ID[] = "parentId";
constexpr char SPAN_TIMESTAMP[] = "timestamp";
constexpr char SPAN_ANNOTATIONS[] = "annotations";
constexpr char SPAN_LOCAL_ENDPOINT[] = "localEndpoint";
constexpr char SPAN_BINARY_ANNOTATIONS[] = "binaryAnnotations";

constexpr char ANNOTATION_VALUE[] = "value";
constexpr char ANNOTATION_ENDPOINT[] = "endpoint";
constexpr char ANNOTATION_TIMESTAMP[] = "timestamp";

constexpr char BINARY_ANNOTATION_KEY[] = "key";
constexpr char BINARY_ANNOTATION_VALUE[] = "value";
constexpr char BINARY_ANNOTATION_ENDPOINT[] = "endpoint";

constexpr char ENDPOINT_PORT[] = "port";
constexpr char ENDPOINT_IPV4[] = "ipv4";
constexpr char ENDPOINT_IPV6[] = "ipv6";
constexpr char ENDPOINT_SERVICE_NAME[] = "serviceName";

} // namespace

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
