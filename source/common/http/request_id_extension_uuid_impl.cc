#include "common/http/request_id_extension_uuid_impl.h"

#include <cstdint>
#include <string>

#include "envoy/http/header_map.h"

#include "common/common/random_generator.h"
#include "common/common/utility.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

void UUIDRequestIDExtension::set(RequestHeaderMap& request_headers, bool force) {
  if (!force && request_headers.RequestId()) {
    return;
  }

  // TODO(PiotrSikora) PERF: Write UUID directly to the header map.
  std::string uuid = random_.uuid();
  ASSERT(!uuid.empty());
  request_headers.setRequestId(uuid);
}

void UUIDRequestIDExtension::setInResponse(ResponseHeaderMap& response_headers,
                                           const RequestHeaderMap& request_headers) {
  if (request_headers.RequestId()) {
    response_headers.setRequestId(request_headers.getRequestIdValue());
  }
}

bool UUIDRequestIDExtension::modBy(const RequestHeaderMap& request_headers, uint64_t& out,
                                   uint64_t mod) {
  if (request_headers.RequestId() == nullptr) {
    return false;
  }
  const std::string uuid(request_headers.getRequestIdValue());
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

TraceStatus UUIDRequestIDExtension::getTraceStatus(const RequestHeaderMap& request_headers) {
  if (request_headers.RequestId() == nullptr) {
    return TraceStatus::NoTrace;
  }
  absl::string_view uuid = request_headers.getRequestIdValue();
  if (uuid.length() != Random::RandomGeneratorImpl::UUID_LENGTH) {
    return TraceStatus::NoTrace;
  }

  switch (uuid[TRACE_BYTE_POSITION]) {
  case TRACE_FORCED:
    return TraceStatus::Forced;
  case TRACE_SAMPLED:
    return TraceStatus::Sampled;
  case TRACE_CLIENT:
    return TraceStatus::Client;
  default:
    return TraceStatus::NoTrace;
  }
}

void UUIDRequestIDExtension::setTraceStatus(RequestHeaderMap& request_headers, TraceStatus status) {
  if (request_headers.RequestId() == nullptr) {
    return;
  }
  absl::string_view uuid_view = request_headers.getRequestIdValue();
  if (uuid_view.length() != Random::RandomGeneratorImpl::UUID_LENGTH) {
    return;
  }
  std::string uuid(uuid_view);

  switch (status) {
  case TraceStatus::Forced:
    uuid[TRACE_BYTE_POSITION] = TRACE_FORCED;
    break;
  case TraceStatus::Client:
    uuid[TRACE_BYTE_POSITION] = TRACE_CLIENT;
    break;
  case TraceStatus::Sampled:
    uuid[TRACE_BYTE_POSITION] = TRACE_SAMPLED;
    break;
  case TraceStatus::NoTrace:
    uuid[TRACE_BYTE_POSITION] = NO_TRACE;
    break;
  }
  request_headers.setRequestId(uuid);
}

} // namespace Http
} // namespace Envoy
