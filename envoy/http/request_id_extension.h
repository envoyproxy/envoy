#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/http/header_map.h"
#include "envoy/tracing/trace_reason.h"

namespace Envoy {
namespace Http {

/**
 * Abstract request id utilities for getting/setting the request IDs and tracing status of requests
 */
class RequestIDExtension {
public:
  virtual ~RequestIDExtension() = default;

  /**
   *  Get the request ID from the request headers.
   * @param request_headers supplies the incoming request headers for retrieving the request ID.
   * @return the string view or nullopt if the request ID is invalid.
   */
  virtual absl::optional<absl::string_view>
  get(const Http::RequestHeaderMap& request_headers) const PURE;

  /**
   * Get and convert the request ID to a 64-bit integer representation for using in modulo, etc.
   * calculations.
   * @param request_headers supplies the incoming request headers for retrieving the request ID.
   * @return the integer or nullopt if the request ID is invalid.
   */
  virtual absl::optional<uint64_t>
  getInteger(const Http::RequestHeaderMap& request_headers) const PURE;

  /**
   * Directly set a request ID into the provided request headers. Override any previous request ID
   * if any.
   * @param request_headers supplies the incoming request headers for setting a request ID.
   * @param force specifies if a new request ID should be forcefully set if one is already present.
   */
  virtual void set(Http::RequestHeaderMap& request_headers, bool force) PURE;

  /**
   * Preserve request ID in response headers if any is set in the request headers.
   * @param response_headers supplies the downstream response headers for setting the request ID.
   * @param request_headers supplies the incoming request headers for retrieving the request ID.
   */
  virtual void setInResponse(Http::ResponseHeaderMap& response_headers,
                             const Http::RequestHeaderMap& request_headers) PURE;

  /**
   * Get the current tracing reason of a request given its headers.
   * @param request_headers supplies the incoming request headers for retrieving the request ID.
   * @return trace reason of the request based on the given headers.
   */
  virtual Tracing::Reason getTraceReason(const Http::RequestHeaderMap& request_headers) PURE;

  /**
   * Set the tracing status of a request.
   * @param request_headers supplies the incoming request headers for setting the trace reason.
   * @param status the trace reason that should be set for this request.
   */
  virtual void setTraceReason(Http::RequestHeaderMap& request_headers, Tracing::Reason reason) PURE;

  /**
   * Get whether to use request_id based sampling policy or not.
   * @return whether to use request_id based sampling policy or not.
   */
  virtual bool useRequestIdForTraceSampling() const PURE;
};

using RequestIDExtensionSharedPtr = std::shared_ptr<RequestIDExtension>;

} // namespace Http
} // namespace Envoy
