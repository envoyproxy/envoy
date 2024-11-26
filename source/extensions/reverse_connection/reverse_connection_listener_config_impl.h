#pragma once
#include <atomic>
#include <cstdint>
#include <list>
#include <memory>

#include "envoy/network/listener.h"

#include "source/extensions/bootstrap/reverse_connection/reverse_conn_global_registry.h"

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

using ReverseConnParamsPtr = Network::ReverseConnectionListenerConfig::ReverseConnParamsPtr;

class ReverseConnectionListenerConfigImpl : public Network::ReverseConnectionListenerConfig {
public:
  ReverseConnectionListenerConfigImpl(ReverseConnParamsPtr params)
      : rc_local_params_(std::move(params)) {}

  ReverseConnParamsPtr& getReverseConnParams() override { return rc_local_params_; }

private:
  // Stores the parameters identifying the local envoy.
  ReverseConnParamsPtr rc_local_params_;
};

} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy
