#pragma once

#include <datadog/logger.h>

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

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
