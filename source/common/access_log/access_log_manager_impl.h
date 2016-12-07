#pragma once

#include "envoy/api/api.h"
#include "envoy/access_log/access_log.h"

namespace AccessLog {

class AccessLogManagerImpl : public AccessLogManager {
public:
  AccessLogManagerImpl(Api::Api& api, Event::Dispatcher& dispatcher, Thread::BasicLockable& lock,
                       Stats::Store& stats_store)
      : api_(api), dispatcher_(dispatcher), lock_(lock), stats_store_(stats_store) {}

  // AccessLog::AccessLogManager
  void reopen() override;
  Filesystem::FilePtr createAccessLog(const std::string& file_name) override;

private:
  Api::Api& api_;
  Event::Dispatcher& dispatcher_;
  Thread::BasicLockable& lock_;
  Stats::Store& stats_store_;
  std::unordered_map<std::string, Filesystem::FilePtr> access_logs_;
};

} // AccessLog
