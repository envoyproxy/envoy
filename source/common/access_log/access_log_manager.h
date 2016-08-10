#pragma once

#include "envoy/access_log/access_log.h"

namespace AccessLog {

class AccessLogManagerImpl : public AccessLogManager {
public:
  virtual ~AccessLogManagerImpl() {}

  // AccessLogManager
  void reopen() override;
  void registerAccessLog(AccessLogPtr accessLog) override;

private:
  std::list<AccessLogPtr> access_logs_;
};

} // AccessLog
