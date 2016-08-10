#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/common/optional.h"
#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/http/header_map.h"
#include "envoy/upstream/upstream.h"

namespace Http {
namespace AccessLog {

enum class FailureReason {
  // No failure.
  None,
  // Local server healthcheck failed.
  FailedLocalHealthCheck,
  // No healthy upstream.
  NoHealthyUpstream,
  // Request timeout on upstream.
  UpstreamRequestTimeout,
  // Local codec level reset was sent on the stream.
  LocalReset,
  // Remote codec level reset was received on the stream.
  UpstreamRemoteReset,
  // Local reset by a connection pool due to an initial connection failure.
  UpstreamConnectionFailure,
  // If the stream was locally reset due to connection termination.
  UpstreamConnectionTermination,
  // The stream was reset because of a resource overflow.
  UpstreamOverflow,
  // No route found for a given request.
  NoRouteFound,
};

/**
 * Additional information about a completed request for logging.
 */
class RequestInfo {
public:
  virtual ~RequestInfo() {}

  /**
   * filter can trigger this callback on failed response to provide more details about
   * failure.
   */
  virtual void onFailedResponse(FailureReason failure_reason) PURE;

  /**
   * filter can trigger this callback when an upstream host has been selected.
   */
  virtual void onUpstreamHostSelected(Upstream::HostDescriptionPtr host) PURE;

  /**
   * @return the time that the first byte of the request was received.
   */
  virtual SystemTime startTime() const PURE;

  /**
   * @return the # of body bytes received in the request.
   */
  virtual uint64_t bytesReceived() const PURE;

  /**
   * @return the protocol of the request.
   */
  virtual const std::string& protocol() const PURE;

  /**
   * @return the response code.
   */
  virtual const Optional<uint32_t>& responseCode() const PURE;

  /**
   * @return the # of body bytes sent in the response.
   */
  virtual uint64_t bytesSent() const PURE;

  /**
   * @return the milliseconds duration of the first byte received to the last byte sent.
   */
  virtual std::chrono::milliseconds duration() const PURE;

  /**
   * @return the failure reason for richer log experience.
   */
  virtual FailureReason failureReason() const PURE;

  /**
   * @return upstream host description.
   */
  virtual Upstream::HostDescriptionPtr upstreamHost() const PURE;

  /**
   * @return whether the request is a health check request or not.
   */
  virtual bool healthCheck() const PURE;

  /**
   * Set whether the request is a health check request or not.
   */
  virtual void healthCheck(bool is_hc) PURE;
};

/**
 * Interface for access log filters.
 */
class Filter {
public:
  virtual ~Filter() {}

  /**
   * Evaluate whether an access log should be written based on request and response data.
   * @return TRUE if the log should be written.
   */
  virtual bool evaluate(const RequestInfo& info, const HeaderMap& request_headers) PURE;
};

typedef std::unique_ptr<Filter> FilterPtr;

/**
 * Abstract access logger for HTTP requests and responses.
 */
class Instance : public ::AccessLog::AccessLog {
public:
  virtual ~Instance() {}

  /**
   * Log a completed request.
   * @param request_headers supplies the incoming request headers after filtering.
   * @param response_headers supplies response headers.
   * @param request_info supplies additional information about the request not contained in
   *                      the request headers.
   */
  virtual void log(const HeaderMap* request_headers, const HeaderMap* response_headers,
                   const RequestInfo& request_info) PURE;
};

typedef std::shared_ptr<Instance> InstancePtr;

/**
 * Interface for access log formatter.
 */
class Formatter {
public:
  virtual ~Formatter() {}

  virtual std::string format(const Http::HeaderMap& request_headers,
                             const Http::HeaderMap& response_headers,
                             const RequestInfo& request_info) const PURE;
};

typedef std::unique_ptr<Formatter> FormatterPtr;

} // AccessLog
} // Http
