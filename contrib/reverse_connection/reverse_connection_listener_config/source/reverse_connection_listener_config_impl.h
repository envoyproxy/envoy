#pragma once
#include <atomic>
#include <cstdint>
#include <list>
#include <memory>

#include "envoy/network/listener.h"

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

class ReverseConnectionListenerConfigImpl : public Network::ReverseConnectionListenerConfig {
public:
  ReverseConnectionListenerConfigImpl(ReverseConnParamsPtr params, Network::RevConnRegistry& registry)
      : rc_local_params_(std::move(params)), registry_(registry) {}

  ReverseConnParamsPtr& getReverseConnParams() override { return rc_local_params_; }

  Network::RevConnRegistry& reverseConnRegistry() override { return registry_;}

private:
  // Stores the parameters identifying the local envoy.
  ReverseConnParamsPtr rc_local_params_;

  // The global reverse connection registry.
  Network::RevConnRegistry& registry_;
};

} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy
