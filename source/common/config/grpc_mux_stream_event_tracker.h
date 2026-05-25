#pragma once

#include "envoy/config/grpc_mux.h"

#include "source/common/common/callback_impl.h"

namespace Envoy {
namespace Config {

/**
 * Helper class to track gRPC mux stream lifecycle events (established/closed)
 * and dispatch callbacks to registered listeners.
 */
class GrpcMuxStreamEventTracker {
public:
  Common::CallbackHandlePtr addStreamEventCallback(GrpcMuxStreamEventCallback callback) {
    return callbacks_.add(std::move(callback));
  }

  bool grpcStreamConnected() const { return connected_; }

  void onStreamEstablished() {
    connected_ = true;
    callbacks_.runCallbacks(GrpcMuxStreamEvent::Established);
  }

  void onStreamClosed() {
    connected_ = false;
    callbacks_.runCallbacks(GrpcMuxStreamEvent::Closed);
  }

private:
  Common::CallbackManager<void, GrpcMuxStreamEvent> callbacks_;
  bool connected_{false};
};

} // namespace Config
} // namespace Envoy
