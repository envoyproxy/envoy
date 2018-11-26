#pragma once

#include <string>
#include <unordered_map>

#include "envoy/access_log/access_log.h"
#include "envoy/api/api.h"

namespace Envoy {
namespace AccessLog {

class AccessLogManagerImpl : public AccessLogManager {
public:
  AccessLogManagerImpl(Api::Api& api, Event::Dispatcher& dispatcher, Thread::BasicLockable& lock)
      : api_(api), dispatcher_(dispatcher), lock_(lock) {}

  // AccessLog::AccessLogManager
  void reopen() override;
  Filesystem::FileSharedPtr createAccessLog(const std::string& file_name) override;

private:
  Api::Api& api_;
  Event::Dispatcher& dispatcher_;
  Thread::BasicLockable& lock_;
  std::unordered_map<std::string, Filesystem::FileSharedPtr> access_logs_;
};

} // namespace AccessLog
} // namespace Envoy
