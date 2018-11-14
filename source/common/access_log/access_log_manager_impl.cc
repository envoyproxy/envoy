#include "common/access_log/access_log_manager_impl.h"

#include <string>

namespace Envoy {
namespace AccessLog {

void AccessLogManagerImpl::reopen() {
  for (auto& access_log : access_logs_) {
    access_log.second->reopen();
  }
}

Filesystem::FileSharedPtr AccessLogManagerImpl::createAccessLog(const std::string& file_name) {
  if (access_logs_.count(file_name)) {
    return access_logs_[file_name];
  }

  access_logs_[file_name] = api_.createFile(file_name, dispatcher_, lock_);
  return access_logs_[file_name];
}

} // namespace AccessLog
} // namespace Envoy
