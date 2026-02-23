#pragma once

#include <cstdint>
#include <functional>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "library/cc/stream_client.h"
#include "library/common/types/c_types.h"

namespace Envoy {
class InternalEngine;
class BaseClientIntegrationTest;

namespace Platform {

class StreamClient;
using StreamClientSharedPtr = std::shared_ptr<StreamClient>;

class Engine : public std::enable_shared_from_this<Engine> {
public:
  // Creates a non-owning Engine wrapper around an existing InternalEngine handle.
  // The returned Engine will not auto-terminate the underlying InternalEngine in its destructor.
  static absl::StatusOr<std::shared_ptr<Engine>>
  createFromInternalEngineHandle(int64_t internal_engine_handle);

  ~Engine();

  std::string dumpStats();
  StreamClientSharedPtr streamClient();
  int64_t getInternalEngineHandle() const;
  void onDefaultNetworkChangeEvent(int network);
  // TODO(abeyad): Remove once migrated to onDefaultNetworkChangeEvent().
  void onDefaultNetworkChanged(int network);
  void onDefaultNetworkUnavailable();
  void onDefaultNetworkAvailable();
  envoy_status_t setProxySettings(absl::string_view host, const uint16_t port);

  envoy_status_t terminate();
  Envoy::InternalEngine* engine() { return engine_; }

private:
  Engine(::Envoy::InternalEngine* engine, bool handle_termination = true);

  // required to access private constructor
  friend class EngineBuilder;
  // required to use envoy_engine_t without exposing it publicly
  friend class StreamPrototype;
  // for testing only
  friend class ::Envoy::BaseClientIntegrationTest;

  Envoy::InternalEngine* engine_;
  const bool handle_termination_;
  StreamClientSharedPtr stream_client_;
};

} // namespace Platform
} // namespace Envoy
