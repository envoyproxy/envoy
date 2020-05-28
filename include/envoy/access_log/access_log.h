#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace AccessLog {

class AccessLogFile {
public:
  virtual ~AccessLogFile() = default;

  /**
   * Write data to the file.
   */
  virtual void write(absl::string_view) PURE;

  /**
   * Reopen the file.
   */
  virtual void reopen() PURE;

  /**
   * Synchronously flush all pending data to disk.
   */
  virtual void flush() PURE;
};

using AccessLogFileSharedPtr = std::shared_ptr<AccessLogFile>;

class AccessLogManager {
public:
  virtual ~AccessLogManager() = default;

  /**
   * Reopen all of the access log files.
   */
  virtual void reopen() PURE;

  /**
   * Create a new access log file managed by the access log manager.
   * @param file_name specifies the file to create/open.
   * @return the opened file.
   */
  virtual AccessLogFileSharedPtr createAccessLog(const std::string& file_name) PURE;
};

using AccessLogManagerPtr = std::unique_ptr<AccessLogManager>;

/**
 * Interface for access log filters.
 */
class Filter {
public:
  virtual ~Filter() = default;

  /**
   * Evaluate whether an access log should be written based on request and response data.
   * @return TRUE if the log should be written.
   */
  virtual bool evaluate(const StreamInfo::StreamInfo& info,
                        const Http::RequestHeaderMap& request_headers,
                        const Http::ResponseHeaderMap& response_headers,
                        const Http::ResponseTrailerMap& response_trailers) const PURE;
};

using FilterPtr = std::unique_ptr<Filter>;

/**
 * Abstract access logger for requests and connections.
 */
class Instance {
public:
  virtual ~Instance() = default;

  /**
   * Log a completed request.
   * @param request_headers supplies the incoming request headers after filtering.
   * @param response_headers supplies response headers.
   * @param response_trailers supplies response trailers.
   * @param stream_info supplies additional information about the request not
   * contained in the request headers.
   */
  virtual void log(const Http::RequestHeaderMap* request_headers,
                   const Http::ResponseHeaderMap* response_headers,
                   const Http::ResponseTrailerMap* response_trailers,
                   const StreamInfo::StreamInfo& stream_info) PURE;
};

using InstanceSharedPtr = std::shared_ptr<Instance>;

} // namespace AccessLog
} // namespace Envoy
