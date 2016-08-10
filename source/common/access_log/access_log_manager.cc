#include "access_log_manager.h"

namespace AccessLog {

void AccessLogManagerImpl::reopen() {
  for (auto& access_log : access_logs_) {
    access_log->reopen();
  }
}

void AccessLogManagerImpl::registerAccessLog(AccessLogPtr accessLog) {
  access_logs_.push_back(accessLog);
}

} // AccessLog
