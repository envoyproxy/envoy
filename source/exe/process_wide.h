#pragma once

namespace Envoy {

// Process-wide lifecycle events for global state in third-party dependencies,
// e.g. gRPC, c-ares. There should only ever be a single instance of this.
class ProcessWide {
public:
  ProcessWide();
  ~ProcessWide();
};

} // namespace Envoy
