#include "common/request_id_extension/uuid_impl.h"

#include <cstdint>
#include <string>

#include "envoy/http/header_map.h"

#include "common/common/utility.h"
#include "common/runtime/runtime_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace RequestIDExtension {

void UUIDUtils::setRequestID(Http::RequestHeaderMap& request_headers) {
  // TODO(PiotrSikora) PERF: Write UUID directly to the header map.
  std::string uuid = random.uuid();
  ASSERT(!uuid.empty());
  request_headers.setRequestId(uuid);
}

void UUIDUtils::ensureRequestID(Http::RequestHeaderMap& request_headers) {
  if (!request_headers.RequestId()) {
    setRequestID(request_headers);
  }
}

void UUIDUtils::preserveRequestIDInResponse(Http::ResponseHeaderMap& response_headers,
                                            const Http::RequestHeaderMap& request_headers) {
  if (request_headers.RequestId()) {
    response_headers.setRequestId(request_headers.RequestId()->value().getStringView());
  }
}

bool UUIDUtils::modRequestIDBy(const Http::RequestHeaderMap& request_headers, uint64_t& out,
                               uint64_t mod) {
  if (request_headers.RequestId() == nullptr) {
    return false;
  }
  const std::string uuid(request_headers.RequestId()->value().getStringView());
  if (uuid.length() < 8) {
    return false;
  }

  uint64_t value;
  if (!StringUtil::atoull(uuid.substr(0, 8).c_str(), value, 16)) {
    return false;
  }

  out = value % mod;
  return true;
}

Envoy::RequestIDExtension::TraceStatus
UUIDUtils::getTraceStatus(const Http::RequestHeaderMap& request_headers) {
  if (request_headers.RequestId() == nullptr) {
    return Envoy::RequestIDExtension::TraceStatus::NoTrace;
  }
  absl::string_view uuid = request_headers.RequestId()->value().getStringView();
  if (uuid.length() != Runtime::RandomGeneratorImpl::UUID_LENGTH) {
    return Envoy::RequestIDExtension::TraceStatus::NoTrace;
  }

  switch (uuid[TRACE_BYTE_POSITION]) {
  case TRACE_FORCED:
    return Envoy::RequestIDExtension::TraceStatus::Forced;
  case TRACE_SAMPLED:
    return Envoy::RequestIDExtension::TraceStatus::Sampled;
  case TRACE_CLIENT:
    return Envoy::RequestIDExtension::TraceStatus::Client;
  default:
    return Envoy::RequestIDExtension::TraceStatus::NoTrace;
  }
}

void UUIDUtils::setTraceStatus(Http::RequestHeaderMap& request_headers,
                               Envoy::RequestIDExtension::TraceStatus status) {
  if (request_headers.RequestId() == nullptr) {
    return;
  }
  absl::string_view uuid_view = request_headers.RequestId()->value().getStringView();
  if (uuid_view.length() != Runtime::RandomGeneratorImpl::UUID_LENGTH) {
    return;
  }
  std::string uuid(uuid_view);

  switch (status) {
  case Envoy::RequestIDExtension::TraceStatus::Forced:
    uuid[TRACE_BYTE_POSITION] = TRACE_FORCED;
    break;
  case Envoy::RequestIDExtension::TraceStatus::Client:
    uuid[TRACE_BYTE_POSITION] = TRACE_CLIENT;
    break;
  case Envoy::RequestIDExtension::TraceStatus::Sampled:
    uuid[TRACE_BYTE_POSITION] = TRACE_SAMPLED;
    break;
  case Envoy::RequestIDExtension::TraceStatus::NoTrace:
    uuid[TRACE_BYTE_POSITION] = NO_TRACE;
    break;
  }
  request_headers.setRequestId(uuid);
}

} // namespace RequestIDExtension
} // namespace Envoy
