#include "common/tracing/zipkin/zipkin_core_constants.h"

namespace Zipkin {

const std::string ZipkinCoreConstants::CLIENT_SEND = "cs";
const std::string ZipkinCoreConstants::CLIENT_RECV = "cr";
const std::string ZipkinCoreConstants::SERVER_SEND = "ss";
const std::string ZipkinCoreConstants::SERVER_RECV = "sr";
const std::string ZipkinCoreConstants::WIRE_SEND = "ws";
const std::string ZipkinCoreConstants::WIRE_RECV = "wr";
const std::string ZipkinCoreConstants::CLIENT_SEND_FRAGMENT = "csf";
const std::string ZipkinCoreConstants::CLIENT_RECV_FRAGMENT = "crf";
const std::string ZipkinCoreConstants::SERVER_SEND_FRAGMENT = "ssf";
const std::string ZipkinCoreConstants::SERVER_RECV_FRAGMENT = "srf";

const std::string ZipkinCoreConstants::HTTP_HOST = "http.host";
const std::string ZipkinCoreConstants::HTTP_METHOD = "http.method";
const std::string ZipkinCoreConstants::HTTP_PATH = "http.path";
const std::string ZipkinCoreConstants::HTTP_URL = "http.url";
const std::string ZipkinCoreConstants::HTTP_STATUS_CODE = "http.status_code";
const std::string ZipkinCoreConstants::HTTP_REQUEST_SIZE = "http.request.size";
const std::string ZipkinCoreConstants::HTTP_RESPONSE_SIZE = "http.response.size";

const std::string ZipkinCoreConstants::LOCAL_COMPONENT = "lc";
const std::string ZipkinCoreConstants::ERROR = "error";
const std::string ZipkinCoreConstants::CLIENT_ADDR = "ca";
const std::string ZipkinCoreConstants::SERVER_ADDR = "sa";

// Zipkin B3 headers
const std::string ZipkinCoreConstants::X_B3_TRACE_ID = "X-B3-TraceId";
const std::string ZipkinCoreConstants::X_B3_SPAN_ID = "X-B3-SpanId";
const std::string ZipkinCoreConstants::X_B3_PARENT_SPAN_ID = "X-B3-ParentSpanId";
const std::string ZipkinCoreConstants::X_B3_SAMPLED = "X-B3-Sampled";
const std::string ZipkinCoreConstants::X_B3_FLAGS = "X-B3-Flags";
} // Zipkin
