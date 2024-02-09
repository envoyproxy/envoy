#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/data/accesslog/v3/accesslog.pb.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/formatter/http_formatter_context.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/protobuf/protobuf.h"

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
   * @param file_info specifies the file to create/open.
   * @return the opened file or an error status.
   */
  virtual absl::StatusOr<AccessLogFileSharedPtr>
  createAccessLog(const Envoy::Filesystem::FilePathAndType& file_info) PURE;
};

using AccessLogManagerPtr = std::unique_ptr<AccessLogManager>;
using AccessLogType = envoy::data::accesslog::v3::AccessLogType;

/**
 * Templated interface for access log filters.
 */
template <class Context> class FilterBase {
public:
  virtual ~FilterBase() = default;

  /**
   * Evaluate whether an access log should be written based on request and response data.
   * @return TRUE if the log should be written.
   */
  virtual bool evaluate(const Context& context, const StreamInfo::StreamInfo& info) const PURE;
};
template <class Context> using FilterBasePtr = std::unique_ptr<FilterBase<Context>>;

/**
 * Templated interface for access log instances.
 * TODO(wbpcode): refactor existing access log instances and related other interfaces to use this
 * interface. See https://github.com/envoyproxy/envoy/issues/28773.
 */
template <class Context> class InstanceBase {
public:
  virtual ~InstanceBase() = default;

  /**
   * Log a completed request.
   * @param context supplies the context for the log.
   * @param stream_info supplies additional information about the request not
   * contained in the request headers.
   */
  virtual void log(const Context& context, const StreamInfo::StreamInfo& stream_info) PURE;
};
template <class Context> using InstanceBaseSharedPtr = std::shared_ptr<InstanceBase<Context>>;

/**
 * Interface for HTTP access log filters.
 */
using Filter = FilterBase<Formatter::HttpFormatterContext>;
using FilterPtr = std::unique_ptr<Filter>;

/**
 * Abstract access logger for HTTP requests and TCP connections.
 */
using Instance = InstanceBase<Formatter::HttpFormatterContext>;
using InstanceSharedPtr = std::shared_ptr<Instance>;

} // namespace AccessLog
} // namespace Envoy
