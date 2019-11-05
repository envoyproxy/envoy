#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

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
  virtual bool evaluate(const StreamInfo::StreamInfo& info, const Http::HeaderMap& request_headers,
                        const Http::HeaderMap& response_headers,
                        const Http::HeaderMap& response_trailers) PURE;
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
   * Prior to logging, call refreshByteSize() on HeaderMaps to ensure that an accurate byte size
   * count is logged.
   * TODO(asraa): Remove refreshByteSize() requirement when entries in HeaderMap can no longer be
   * modified by reference and headerMap holds an accurate internal byte size count.
   * @param request_headers supplies the incoming request headers after filtering.
   * @param response_headers supplies response headers.
   * @param response_trailers supplies response trailers.
   * @param stream_info supplies additional information about the request not
   * contained in the request headers.
   */
  virtual void log(const Http::HeaderMap* request_headers, const Http::HeaderMap* response_headers,
                   const Http::HeaderMap* response_trailers,
                   const StreamInfo::StreamInfo& stream_info) PURE;
};

using InstanceSharedPtr = std::shared_ptr<Instance>;

/**
 * Interface for access log formatter.
 * Formatters provide a complete access log output line for the given headers/trailers/stream.
 */
class Formatter {
public:
  virtual ~Formatter() = default;

  /**
   * Return a formatted access log line.
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param stream_info supplies the stream info.
   * @return std::string string containing the complete formatted access log line.
   */
  virtual std::string format(const Http::HeaderMap& request_headers,
                             const Http::HeaderMap& response_headers,
                             const Http::HeaderMap& response_trailers,
                             const StreamInfo::StreamInfo& stream_info,
                             const absl::string_view& response_body) const PURE;
};

using FormatterPtr = std::unique_ptr<Formatter>;

/**
 * Interface for access log provider.
 * FormatterProviders extract information from the given headers/trailers/stream.
 */
class FormatterProvider {
public:
  virtual ~FormatterProvider() = default;

  /**
   * Extract a value from the provided headers/trailers/stream.
   * @param request_headers supplies the request headers.
   * @param response_headers supplies the response headers.
   * @param response_trailers supplies the response trailers.
   * @param stream_info supplies the stream info.
   * @return std::string containing a single value extracted from the given headers/trailers/stream.
   */
  virtual std::string format(const Http::HeaderMap& request_headers,
                             const Http::HeaderMap& response_headers,
                             const Http::HeaderMap& response_trailers,
                             const StreamInfo::StreamInfo& stream_info,
                             const absl::string_view& response_body) const PURE;
};

using FormatterProviderPtr = std::unique_ptr<FormatterProvider>;

} // namespace AccessLog
} // namespace Envoy
