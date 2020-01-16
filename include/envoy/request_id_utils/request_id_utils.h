#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace RequestIDUtils {

enum class TraceStatus { NoTrace, Sampled, Client, Forced };

/**
 * Abstract request id utilities for getting/setting the request IDs and tracing status of requests
 */
class Utilities {
public:
  virtual ~Utilities() = default;

  /**
   * Directly set a request ID into the provided request headers
   * @param request_headers supplies the incoming request headers for setting a request ID.
   */
  virtual void setRequestID(Http::HeaderMap& request_headers) PURE;

  /**
   * Ensure that a request is configured with a request id.
   * @param request_headers supplies the incoming request headers for setting a request ID.
   */
  virtual void ensureRequestID(Http::HeaderMap& request_headers) PURE;

  /**
   * Perform a mod operation across the request id within a request and store the result in the
   * provided output variable. This is used to perform sampling and validate the request ID.
   * @param request_headers supplies the incoming request headers for retrieving the request ID.
   * @param out reference to a variable where we store the result of the mod operation.
   * @param mod integer to mod the request ID by.
   */
  virtual bool modRequestIDBy(const Http::HeaderMap& request_headers, uint64_t& out,
                              uint64_t mod) PURE;

  /**
   * Get the current tracing status of a request given its headers.
   * @param request_headers supplies the incoming request headers for retrieving the request ID.
   */
  virtual TraceStatus getTraceStatus(const Http::HeaderMap& request_headers) PURE;

  /**
   * Set the tracing status of a request.
   * @param request_headers supplies the incoming request headers for setting the trace status.
   * @param status the trace status that should be set for this request.
   */
  virtual void setTraceStatus(Http::HeaderMap& request_headers, const TraceStatus status) PURE;
};

using UtilitiesSharedPtr = std::shared_ptr<Utilities>;
} // namespace RequestIDUtils
} // namespace Envoy
