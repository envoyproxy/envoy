#pragma once

#include "envoy/common/pure.h"

namespace AccessLog {

class AccessLog {
public:
  virtual ~AccessLog() {}

  /**
   * Reopen access log file.
   */
  virtual void reopen() PURE;
};

typedef std::shared_ptr<AccessLog> AccessLogPtr;

class AccessLogManager {
public:
  virtual ~AccessLogManager() {}

  /**
   * Reopen all of the access log files.
   */
  virtual void reopen() PURE;

  /**
   * Register access log to manager.
   */
  virtual void registerAccessLog(AccessLogPtr accessLog) PURE;
};

typedef std::unique_ptr<AccessLogManager> AccessLogManagerPtr;

} // AccessLog
