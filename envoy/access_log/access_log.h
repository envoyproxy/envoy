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

using LogContext = Formatter::Context;

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
 * Interface for access log filters.
 */
class Filter {
public:
  virtual ~Filter() = default;

  /**
   * Evaluate whether an access log should be written based on request and response data.
   * @return TRUE if the log should be written.
   */
  virtual bool evaluate(const LogContext& context, const StreamInfo::StreamInfo& info) const PURE;
};
using FilterPtr = std::unique_ptr<Filter>;

/**
 * Interface for access log instances.
 */
class Instance {
public:
  virtual ~Instance() = default;

  /**
   * Log a completed request.
   * @param context supplies the context for the log.
   * @param stream_info supplies additional information about the request not
   * contained in the request headers.
   */
  virtual void log(const LogContext& context, const StreamInfo::StreamInfo& stream_info) PURE;
};
using InstanceSharedPtr = std::shared_ptr<Instance>;
using InstanceSharedPtrVector = std::vector<InstanceSharedPtr>;

} // namespace AccessLog
} // namespace Envoy
