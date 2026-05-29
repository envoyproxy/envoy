#pragma once

#include <sys/types.h>

#include "envoy/access_log/access_log.h"

#include "source/extensions/access_loggers/common/access_log_base.h"
#include "source/extensions/access_loggers/dynamic_modules/access_log_config.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace DynamicModules {

/**
 * Thread-local logger wrapper for per-thread module instances.
 */
struct ThreadLocalLogger : public ThreadLocal::ThreadLocalObject {
  ThreadLocalLogger(envoy_dynamic_module_type_access_logger_module_ptr logger,
                    DynamicModuleAccessLogConfigSharedPtr config, uint32_t worker_index);
  ~ThreadLocalLogger() override;

public:
  /**
   * Helper to get the `this` pointer as a void pointer.
   */
  void* thisAsVoidPtr() { return static_cast<void*>(this); }

public:
  envoy_dynamic_module_type_access_logger_module_ptr logger_;
  DynamicModuleAccessLogConfigSharedPtr config_;
  const Formatter::Context* log_context_ = nullptr;
  const StreamInfo::StreamInfo* stream_info_ = nullptr;
  uint32_t worker_index_;
};

/**
 * Access log instance that delegates to a dynamic module.
 */
class DynamicModuleAccessLog : public Common::ImplBase {
public:
  DynamicModuleAccessLog(AccessLog::FilterPtr&& filter,
                         DynamicModuleAccessLogConfigSharedPtr config,
                         ThreadLocal::SlotAllocator& tls);

private:
  void emitLog(const Formatter::Context& context,
               const StreamInfo::StreamInfo& stream_info) override;

  DynamicModuleAccessLogConfigSharedPtr config_;
  ThreadLocal::SlotPtr tls_slot_;
};

} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
