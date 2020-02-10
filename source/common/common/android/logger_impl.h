#pragma once

#include "common/common/base_logger.h"

namespace Envoy {
namespace Logger {

#define GENERATE_LOGGER(X) AndroidLogger(#X),

/**
 * Logger that uses spdlog::sinks::android_sink.
 */
class AndroidLogger : public Logger {
private:
  AndroidLogger(const std::string& name);

  friend class Registry;
};

} // namespace Logger
} // namespace Envoy
