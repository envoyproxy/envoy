#include "source/extensions/request_id/uuid/config.h"

#include "envoy/http/header_map.h"
#include "envoy/tracing/tracer.h"

#include "source/common/common/random_generator.h"
#include "source/common/common/utility.h"
#include "source/common/stream_info/stream_id_provider_impl.h"

namespace Envoy {
namespace Extensions {
namespace RequestId {

void UUIDRequestIDExtension::set(Http::RequestHeaderMap& request_headers, bool edge_request,
                                 bool keep_external_id) {
  const Http::HeaderEntry* request_id_header = request_headers.RequestId();

  // No request ID then set new one anyway.
  if (request_id_header == nullptr || request_id_header->value().empty()) {
    request_headers.setRequestId(random_.uuid());
    return;
  }

  // There is request ID already set and this is not an edge request. Then this is trusted
  // request ID. Do nothing.
  if (!edge_request) {
    return;
  }

  // There is request ID already set and this is an edge request. Then this is ID may cannot
  // be trusted.

  if (!keep_external_id) {
    // If we are not keeping external request ID, then set new one anyway.
    request_headers.setRequestId(random_.uuid());
    return;
  }

  // If we are keeping external request ID, and `pack_trace_reason` is enabled, then clear
  // the trace reason in the external request ID.
  if (pack_trace_reason_) {
    setTraceReason(request_headers, Tracing::Reason::NotTraceable);
  }
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
  // If the request ID is not present or the pack trace reason is not enabled, return
  // NotTraceable directly.
  if (!pack_trace_reason_ || request_headers.RequestId() == nullptr) {
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
