#include "zipkin_core_constants.h"

const std::string Zipkin::ZipkinCoreConstants::CLIENT_SEND = "cs";

const std::string Zipkin::ZipkinCoreConstants::CLIENT_RECV = "cr";

const std::string Zipkin::ZipkinCoreConstants::SERVER_SEND = "ss";

const std::string Zipkin::ZipkinCoreConstants::SERVER_RECV = "sr";

const std::string Zipkin::ZipkinCoreConstants::WIRE_SEND = "ws";

const std::string Zipkin::ZipkinCoreConstants::WIRE_RECV = "wr";

const std::string Zipkin::ZipkinCoreConstants::CLIENT_SEND_FRAGMENT = "csf";

const std::string Zipkin::ZipkinCoreConstants::CLIENT_RECV_FRAGMENT = "crf";

const std::string Zipkin::ZipkinCoreConstants::SERVER_SEND_FRAGMENT = "ssf";

const std::string Zipkin::ZipkinCoreConstants::SERVER_RECV_FRAGMENT = "srf";

const std::string Zipkin::ZipkinCoreConstants::HTTP_HOST = "http.host";

const std::string Zipkin::ZipkinCoreConstants::HTTP_METHOD = "http.method";

const std::string Zipkin::ZipkinCoreConstants::HTTP_PATH = "http.path";

const std::string Zipkin::ZipkinCoreConstants::HTTP_URL = "http.url";

const std::string Zipkin::ZipkinCoreConstants::HTTP_STATUS_CODE = "http.status_code";

const std::string Zipkin::ZipkinCoreConstants::HTTP_REQUEST_SIZE = "http.request.size";

const std::string Zipkin::ZipkinCoreConstants::HTTP_RESPONSE_SIZE = "http.response.size";

const std::string Zipkin::ZipkinCoreConstants::LOCAL_COMPONENT = "lc";

const std::string Zipkin::ZipkinCoreConstants::ERROR = "error";

const std::string Zipkin::ZipkinCoreConstants::CLIENT_ADDR = "ca";

const std::string Zipkin::ZipkinCoreConstants::SERVER_ADDR = "sa";

// Zipkin B3 headers
const std::string Zipkin::ZipkinCoreConstants::X_B3_TRACE_ID = "X-B3-TraceId";

const std::string Zipkin::ZipkinCoreConstants::X_B3_SPAN_ID = "X-B3-SpanId";

const std::string Zipkin::ZipkinCoreConstants::X_B3_PARENT_SPAN_ID = "X-B3-ParentSpanId";

const std::string Zipkin::ZipkinCoreConstants::X_B3_SAMPLED = "X-B3-Sampled";

const std::string Zipkin::ZipkinCoreConstants::X_B3_FLAGS = "X-B3-Flags";
