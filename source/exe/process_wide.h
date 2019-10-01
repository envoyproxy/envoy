#pragma once

#include <cstdint>

namespace Envoy {

// Process-wide lifecycle events for global state in third-party dependencies,
// e.g. c-ares. There should only ever be a single instance of this.
class ProcessWide {
public:
  ProcessWide();
  ~ProcessWide();

private:
  uint32_t initialization_depth_;
};

} // namespace Envoy
