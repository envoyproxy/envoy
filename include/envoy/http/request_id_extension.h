#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Http {

enum class TraceStatus { NoTrace, Sampled, Client, Forced };

/**
 * Abstract request id utilities for getting/setting the request IDs and tracing status of requests
 */
class RequestIDExtension {
public:
  virtual ~RequestIDExtension() = default;

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
   * Perform a mod operation across the request id within a request and store the result in the
   * provided output variable. This is used to perform sampling and validate the request ID.
   * @param request_headers supplies the incoming request headers for retrieving the request ID.
   * @param out reference to a variable where we store the result of the mod operation.
   * @param mod integer to mod the request ID by.
   * @return true if request ID is valid and out is populated by the result.
   */
  virtual bool modBy(const Http::RequestHeaderMap& request_headers, uint64_t& out,
                     uint64_t mod) PURE;

  /**
   * Get the current tracing status of a request given its headers.
   * @param request_headers supplies the incoming request headers for retrieving the request ID.
   * @return trace status of the request based on the given headers.
   */
  virtual TraceStatus getTraceStatus(const Http::RequestHeaderMap& request_headers) PURE;

  /**
   * Set the tracing status of a request.
   * @param request_headers supplies the incoming request headers for setting the trace status.
   * @param status the trace status that should be set for this request.
   */
  virtual void setTraceStatus(Http::RequestHeaderMap& request_headers, TraceStatus status) PURE;
};

using RequestIDExtensionSharedPtr = std::shared_ptr<RequestIDExtension>;

} // namespace Http
} // namespace Envoy
