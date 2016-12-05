#include "access_log_manager_impl.h"

namespace AccessLog {

void AccessLogManagerImpl::reopen() {
  for (auto& access_log : access_logs_) {
    access_log->reopen();
  }
}

Filesystem::FilePtr AccessLogManagerImpl::createAccessLog(const std::string& file_name) {
  access_logs_.push_back(api_.createFile(file_name, dispatcher_, lock_, stats_store_));
  return access_logs_.back();
}

} // AccessLog
