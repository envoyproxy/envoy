#pragma once

namespace Envoy {
namespace Platform {

// Interface for monitoring network changes.
class NetworkChangeMonitor {
public:
  virtual ~NetworkChangeMonitor() = default;

  // Starts monitoring network changes.
  virtual void start() = 0;

  // Stops monitoring network changes.
  virtual void stop() = 0;
};

// Interface for listening to network change events.
class NetworkChangeListener {
public:
  virtual ~NetworkChangeListener() = default;

  // Called when a network change event occurs. `network` contains the new network type.
  virtual void onDefaultNetworkChangeEvent(int network) = 0;

  // Called when the default network becomes available.
  virtual void onDefaultNetworkAvailable() = 0;

  // Called when the default network becomes unavailable.
  virtual void onDefaultNetworkUnavailable() = 0;
};

} // namespace Platform
} // namespace Envoy
