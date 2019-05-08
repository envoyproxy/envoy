#pragma once

#include "common/common/cleanup.h"

namespace Envoy {

// Process-wide lifecycle events for global state in third-party dependencies,
// e.g. gRPC, c-ares.
class ProcessWide {
public:
  // Process-wide setup. This returns a Cleanup object, which will perform
  // process-wide shutdown when destroyed via the RAII pattern.
  static CleanupPtr setup();
};

} // namespace Envoy
