#pragma once

#include "source/common/common/logger.h"

#include "datadog/logger.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

/**
 * Logging adapter for use by dd-trace-cpp. This class implements dd-trace-cpp's
 * datadog::tracing::Logger interface in terms of spdlog::logger, which is what
 * is used by Envoy. An instance of this class is passed into dd-trace-cpp when
 * tracing is configured, allowing dd-trace-cpp to produce Envoy logs.
 */
class Logger : public datadog::tracing::Logger {
public:
  explicit Logger(spdlog::logger& logger);

  // datadog::tracing::Logger

  void log_error(const LogFunc&) override;
  void log_startup(const LogFunc&) override;

  void log_error(const datadog::tracing::Error&) override;
  void log_error(datadog::tracing::StringView) override;

private:
  spdlog::logger& logger_;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
