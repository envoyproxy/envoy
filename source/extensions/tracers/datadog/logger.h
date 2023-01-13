#pragma once

#include <datadog/logger.h>

#include "source/common/common/logger.h"
#include "source/extensions/tracers/datadog/dd.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

class Logger : public dd::Logger {
  spdlog::logger* logger_;

public:
  explicit Logger(spdlog::logger& logger);

  // dd::Logger

  void log_error(const LogFunc&) override;
  void log_startup(const LogFunc&) override;

  void log_error(const dd::Error&) override;
  void log_error(dd::StringView) override;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
