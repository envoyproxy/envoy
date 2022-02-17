#pragma once

#include "source/common/common/base_logger.h"

namespace Envoy {
namespace Logger {

#define GENERATE_LOGGER(X) StandardLogger(#X),

} // namespace Logger
} // namespace Envoy
