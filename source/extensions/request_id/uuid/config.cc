#include "source/extensions/request_id/uuid/config.h"

#include "envoy/http/header_map.h"
#include "envoy/tracing/http_tracer.h"

#include "source/common/common/random_generator.h"
#include "source/common/common/utility.h"
#include "source/common/stream_info/stream_id_provider_impl.h"

namespace Envoy {
namespace Extensions {
namespace RequestId {

void UUIDRequestIDExtension::set(Http::RequestHeaderMap& request_headers, bool force) {
  if (!force && request_headers.RequestId()) {
    return;
  }

  // TODO(PiotrSikora) PERF: Write UUID directly to the header map.
  std::string uuid = random_.uuid();
  ASSERT(!uuid.empty());
  request_headers.setRequestId(uuid);
}

void UUIDRequestIDExtension::setInResponse(Http::ResponseHeaderMap& response_headers,
                                           const Http::RequestHeaderMap& request_headers) {
  if (request_headers.RequestId()) {
    response_headers.setRequestId(request_headers.getRequestIdValue());
  }
}

absl::optional<absl::string_view>
UUIDRequestIDExtension::get(const Http::RequestHeaderMap& request_headers) const {
  if (request_headers.RequestId() == nullptr) {
    return absl::nullopt;
  }
  return request_headers.getRequestIdValue();
}

absl::optional<uint64_t>
UUIDRequestIDExtension::getInteger(const Http::RequestHeaderMap& request_headers) const {
  if (request_headers.RequestId() == nullptr) {
    return absl::nullopt;
  }
  const std::string uuid(request_headers.getRequestIdValue());
  if (uuid.length() < 8) {
    return absl::nullopt;
  }

  uint64_t value;
  if (!StringUtil::atoull(uuid.substr(0, 8).c_str(), value, 16)) {
    return absl::nullopt;
  }

  return value;
}

Tracing::Reason
UUIDRequestIDExtension::getTraceReason(const Http::RequestHeaderMap& request_headers) {
  if (request_headers.RequestId() == nullptr) {
    return Tracing::Reason::NotTraceable;
  }
  absl::string_view uuid = request_headers.getRequestIdValue();
  if (uuid.length() != Random::RandomGeneratorImpl::UUID_LENGTH) {
    return Tracing::Reason::NotTraceable;
  }

  switch (uuid[TRACE_BYTE_POSITION]) {
  case TRACE_FORCED:
    return Tracing::Reason::ServiceForced;
  case TRACE_SAMPLED:
    return Tracing::Reason::Sampling;
  case TRACE_CLIENT:
    return Tracing::Reason::ClientForced;
  default:
    return Tracing::Reason::NotTraceable;
  }
}

void UUIDRequestIDExtension::setTraceReason(Http::RequestHeaderMap& request_headers,
                                            Tracing::Reason reason) {
  if (!pack_trace_reason_ || request_headers.RequestId() == nullptr) {
    return;
  }
  absl::string_view uuid_view = request_headers.getRequestIdValue();
  if (uuid_view.length() != Random::RandomGeneratorImpl::UUID_LENGTH) {
    return;
  }
  std::string uuid(uuid_view);

  switch (reason) {
  case Tracing::Reason::ServiceForced:
    uuid[TRACE_BYTE_POSITION] = TRACE_FORCED;
    break;
  case Tracing::Reason::ClientForced:
    uuid[TRACE_BYTE_POSITION] = TRACE_CLIENT;
    break;
  case Tracing::Reason::Sampling:
    uuid[TRACE_BYTE_POSITION] = TRACE_SAMPLED;
    break;
  case Tracing::Reason::NotTraceable:
    uuid[TRACE_BYTE_POSITION] = NO_TRACE;
    break;
  default:
    break;
  }
  request_headers.setRequestId(uuid);
}

REGISTER_FACTORY(UUIDRequestIDExtensionFactory, Server::Configuration::RequestIDExtensionFactory);

} // namespace RequestId
} // namespace Extensions
} // namespace Envoy
