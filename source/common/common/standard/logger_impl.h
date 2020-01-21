#pragma once

namespace Envoy {
namespace Logger {

#define GENERATE_LOGGER(X) StandardLogger(#X),

} // namespace Logger
} // namespace Envoy