#include "cxx_swift_interop.h"

#include "source/server/options_impl.h"

#include "library/common/engine.h"

namespace Envoy {
namespace CxxSwift {

spdlog::level::level_enum logLevelToSpdlog(Platform::LogLevel log_level) {
  switch (log_level) {
  case Logger::Logger::trace:
    return spdlog::level::trace;
  case Logger::Logger::debug:
    return spdlog::level::debug;
  case Logger::Logger::info:
    return spdlog::level::info;
  case Logger::Logger::warn:
    return spdlog::level::warn;
  case Logger::Logger::error:
    return spdlog::level::err;
  case Logger::Logger::critical:
    return spdlog::level::critical;
  case Logger::Logger::off:
    return spdlog::level::off;
  }
}

void run(BootstrapPtr bootstrap_ptr, Platform::LogLevel log_level, envoy_engine_t engine_handle) {
  auto options = std::make_unique<Envoy::OptionsImpl>();
  auto bootstrap =
      absl::WrapUnique(reinterpret_cast<envoy::config::bootstrap::v3::Bootstrap*>(bootstrap_ptr));
  options->setConfigProto(std::move(bootstrap));
  options->setLogLevel(logLevelToSpdlog(log_level));
  options->setConcurrency(1);
  reinterpret_cast<Envoy::Engine*>(engine_handle)->run(std::move(options));
}

} // namespace CxxSwift
} // namespace Envoy
