#pragma once

#include "envoy/config/trace/v3/skywalking.pb.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/tracer_config.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class SkyWalkingClientConfig {
public:
  SkyWalkingClientConfig(Server::Configuration::TracerFactoryContext& context,
                         const envoy::config::trace::v3::ClientConfig& config);

  uint32_t maxCacheSize() const { return max_cache_size_; }

  const std::string& service() const { return service_; }
  const std::string& serviceInstance() const { return instance_; }

  const std::string& backendToken() const;

private:
  Server::Configuration::ServerFactoryContext& factory_context_;

  const uint32_t max_cache_size_{0};

  const std::string service_;
  const std::string instance_;

  std::string backend_token_;
};

using SkyWalkingClientConfigPtr = std::unique_ptr<SkyWalkingClientConfig>;

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
