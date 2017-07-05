#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/filesystem/filesystem.h"

namespace Envoy {
namespace AccessLog {

class AccessLogManager {
public:
  virtual ~AccessLogManager() {}

  /**
   * Reopen all of the access log files.
   */
  virtual void reopen() PURE;

  /**
   * Create a new access log file managed by the access log manager.
   * @param file_name specifies the file to create/open.
   * @return the opened file.
   */
  virtual Filesystem::FileSharedPtr createAccessLog(const std::string& file_name) PURE;
};

typedef std::unique_ptr<AccessLogManager> AccessLogManagerPtr;

} // namespace AccessLog
} // namespace Envoy
