#pragma once

namespace Envoy::Platform {

// Interface for monitoring network changes.
class NetworkChangeMonitor {
public:
  virtual ~NetworkChangeMonitor() = default;

  // Starts monitoring network changes.
  virtual void start() = 0;

  // Stops monitoring network changes.
  virtual void stop() = 0;
};

} // namespace Envoy::Platform
