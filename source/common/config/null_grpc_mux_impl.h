#pragma once

#include "envoy/config/grpc_mux.h"

namespace Envoy {
namespace Config {

// A placeholder class returned if no ADS is configured.
class NullGrpcMuxImpl : public GrpcMux,
                        GrpcStreamCallbacks<envoy::service::discovery::v3::DiscoveryResponse> {
public:
  void start() override {}
  ScopedResume pause(const std::string&) override {
    return std::make_unique<Cleanup>([] {});
  }
  ScopedResume pause(const std::vector<std::string>) override {
    return std::make_unique<Cleanup>([] {});
  }

  GrpcMuxWatchPtr addWatch(const std::string&, const absl::flat_hash_set<std::string>&,
                           SubscriptionCallbacks&, OpaqueResourceDecoderSharedPtr,
                           const SubscriptionOptions&) override {
    ExceptionUtil::throwEnvoyException("ADS must be configured to support an ADS config source");
  }

  void requestOnDemandUpdate(const std::string&, const absl::flat_hash_set<std::string>&) override {
    ENVOY_BUG(false, "unexpected request for on demand update");
  }

  EdsResourcesCacheOptRef edsResourcesCache() override { return absl::nullopt; }

  void onWriteable() override {}
  void onStreamEstablished() override {}
  void onEstablishmentFailure() override {}
  void onDiscoveryResponse(std::unique_ptr<envoy::service::discovery::v3::DiscoveryResponse>&&,
                           ControlPlaneStats&) override {}
};

} // namespace Config
} // namespace Envoy
