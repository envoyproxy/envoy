#pragma once

#include "common/access_log/access_log_manager_impl.h"

namespace Envoy {
namespace AccessLog {

/**
 * Validation-only access log manager that runs as much of the same code as the real thing as
 * possible, except that it creates fake files that can't actually be written to.
 */
class NullAccessLogManager : public AccessLogManagerImpl {
public:
  using AccessLogManagerImpl::AccessLogManagerImpl;

  // Replace AccessLogManagerImpl version with one that returns non-functional files.
  AccessLogFileSharedPtr createAccessLog(const std::string& file_name) override;
};

} // namespace AccessLog
} // namespace Envoy