#include "source/extensions/access_loggers/dynamic_modules/access_log.h"

#include "envoy/common/exception.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace DynamicModules {

ThreadLocalLogger::ThreadLocalLogger(envoy_dynamic_module_type_access_logger_module_ptr logger,
                                     DynamicModuleAccessLogConfigSharedPtr config,
                                     uint32_t worker_index)
    : logger_(logger), config_(config), worker_index_(worker_index) {}

ThreadLocalLogger::~ThreadLocalLogger() {
  if (logger_ != nullptr) {
    // Flush any buffered logs before destroying the logger.
    if (config_->on_logger_flush_ != nullptr) {
      config_->on_logger_flush_(thisAsVoidPtr(), logger_);
    }
    config_->on_logger_destroy_(thisAsVoidPtr(), logger_);
  }
}

DynamicModuleAccessLog::DynamicModuleAccessLog(AccessLog::FilterPtr&& filter,
                                               DynamicModuleAccessLogConfigSharedPtr config,
                                               ThreadLocal::SlotAllocator& tls)
    : Common::ImplBase(std::move(filter)), config_(config), tls_slot_(tls.allocateSlot()) {

  tls_slot_->set([config](Event::Dispatcher& dispatcher) {
    auto worker_thread_index = dispatcher.workerThreadIndex().value_or(0);
    // Create a thread-local logger wrapper first, then pass it to the module.
    auto tl_logger = std::make_shared<ThreadLocalLogger>(nullptr, config, worker_thread_index);
    auto logger = config->on_logger_new_(config->in_module_config_, tl_logger->thisAsVoidPtr());
    tl_logger->logger_ = logger;
    return tl_logger;
  });
}

void DynamicModuleAccessLog::emitLog(const Formatter::Context& context,
                                     const StreamInfo::StreamInfo& stream_info) {
  auto& tl_logger = tls_slot_->getTyped<ThreadLocalLogger>();
  if (tl_logger.logger_ == nullptr) {
    return;
  }

  tl_logger.log_context_ = &context;
  tl_logger.stream_info_ = &stream_info;

  // Convert AccessLogType to ABI enum. The cast is safe because enum values are aligned.
  const auto abi_log_type =
      static_cast<envoy_dynamic_module_type_access_log_type>(context.accessLogType());

  // Invoke the module's log callback with the context pointer.
  config_->on_logger_log_(tl_logger.thisAsVoidPtr(), tl_logger.logger_, abi_log_type);
}

} // namespace DynamicModules
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
