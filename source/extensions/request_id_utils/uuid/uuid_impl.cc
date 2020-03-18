#include "extensions/request_id_utils/uuid/uuid_impl.h"

#include <cstdint>
#include <string>

#include "envoy/http/header_map.h"

#include "common/common/utility.h"
#include "common/runtime/runtime_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace RequestIDUtils {

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

Envoy::RequestIDUtils::TraceStatus
UUIDUtils::getTraceStatus(const Http::RequestHeaderMap& request_headers) {
  if (request_headers.RequestId() == nullptr) {
    return Envoy::RequestIDUtils::TraceStatus::NoTrace;
  }
  absl::string_view uuid = request_headers.RequestId()->value().getStringView();
  if (uuid.length() != Runtime::RandomGeneratorImpl::UUID_LENGTH) {
    return Envoy::RequestIDUtils::TraceStatus::NoTrace;
  }

  switch (uuid[TRACE_BYTE_POSITION]) {
  case TRACE_FORCED:
    return Envoy::RequestIDUtils::TraceStatus::Forced;
  case TRACE_SAMPLED:
    return Envoy::RequestIDUtils::TraceStatus::Sampled;
  case TRACE_CLIENT:
    return Envoy::RequestIDUtils::TraceStatus::Client;
  default:
    return Envoy::RequestIDUtils::TraceStatus::NoTrace;
  }
}

void UUIDUtils::setTraceStatus(Http::RequestHeaderMap& request_headers,
                               const Envoy::RequestIDUtils::TraceStatus status) {
  if (request_headers.RequestId() == nullptr) {
    return;
  }
  absl::string_view uuid_view = request_headers.RequestId()->value().getStringView();
  if (uuid_view.length() != Runtime::RandomGeneratorImpl::UUID_LENGTH) {
    return;
  }
  std::string uuid(uuid_view);

  switch (status) {
  case Envoy::RequestIDUtils::TraceStatus::Forced:
    uuid[TRACE_BYTE_POSITION] = TRACE_FORCED;
    break;
  case Envoy::RequestIDUtils::TraceStatus::Client:
    uuid[TRACE_BYTE_POSITION] = TRACE_CLIENT;
    break;
  case Envoy::RequestIDUtils::TraceStatus::Sampled:
    uuid[TRACE_BYTE_POSITION] = TRACE_SAMPLED;
    break;
  case Envoy::RequestIDUtils::TraceStatus::NoTrace:
    uuid[TRACE_BYTE_POSITION] = NO_TRACE;
    break;
  }
  request_headers.setRequestId(uuid);
}
} // namespace RequestIDUtils
} // namespace Extensions
} // namespace Envoy
