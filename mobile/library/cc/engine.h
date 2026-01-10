#pragma once

#include <functional>
#include <memory>

#include "absl/strings/string_view.h"
#include "library/cc/network_change_monitor.h"
#include "library/cc/stream_client.h"
#include "library/common/types/c_types.h"

namespace Envoy {
class InternalEngine;
class BaseClientIntegrationTest;

namespace Platform {

class StreamClient;
using StreamClientSharedPtr = std::shared_ptr<StreamClient>;

class Engine : public std::enable_shared_from_this<Engine>, public NetworkChangeListener {
public:
  ~Engine();

  std::string dumpStats();
  StreamClientSharedPtr streamClient();
  void initializeNetworkChangeMonitor();
  void onDefaultNetworkChangeEvent(int network);
  // TODO(abeyad): Remove once migrated to onDefaultNetworkChangeEvent().
  void onDefaultNetworkChanged(int network);
  void onDefaultNetworkUnavailable();
  void onDefaultNetworkAvailable();
  envoy_status_t setProxySettings(absl::string_view host, const uint16_t port);

  envoy_status_t terminate();
  Envoy::InternalEngine* engine() { return engine_; }

private:
  Engine(::Envoy::InternalEngine* engine);

  // required to access private constructor
  friend class EngineBuilder;
  // required to use envoy_engine_t without exposing it publicly
  friend class StreamPrototype;
  // for testing only
  friend class ::Envoy::BaseClientIntegrationTest;

  Envoy::InternalEngine* engine_;
  StreamClientSharedPtr stream_client_;
  std::unique_ptr<NetworkChangeMonitor> network_change_monitor_;
};

} // namespace Platform
} // namespace Envoy
