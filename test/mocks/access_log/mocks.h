#pragma once

#include "envoy/access_log/access_log.h"

namespace AccessLog {

class MockAccessLog : public AccessLog {
public:
  MockAccessLog();
  ~MockAccessLog();

  // AccessLog::AccessLog
  MOCK_METHOD0(reopen, void());
};

class MockAccessLogManager : public AccessLogManager {
public:
  MockAccessLogManager();
  ~MockAccessLogManager();

  // AccessLog::AccessLogManager
  MOCK_METHOD0(reopen, void());
  MOCK_METHOD1(registerAccessLog, void(AccessLogPtr));
};

} // AccessLog
