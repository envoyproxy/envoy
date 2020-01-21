#include "common/common/logger_impl.h"

#include "spdlog/sinks/android_sink.h"

namespace Envoy {
namespace Logger {

#define GENERATE_LOGGER(X) AndroidLogger(#X),

AndroidLogger::AndroidLogger(const std::string& name)
    : Logger(std::make_shared<spdlog::logger>(
          name, std::make_shared<spdlog::sinks::android_sink<std::mutex>>())) {}

} // namespace Logger
} // namespace Envoy