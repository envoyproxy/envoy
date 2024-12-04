#pragma once

#include "envoy/config/xds_manager.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/thread.h"

namespace Envoy {
namespace Config {

class XdsManagerImpl : public XdsManager {
public:
  XdsManagerImpl(Upstream::ClusterManager& cm,
                 ProtobufMessage::ValidationContext& validation_context)
      : cm_(cm), validation_context_(validation_context) {}

  // Config::ConfigSourceProvider
  absl::Status
  setAdsConfigSource(const envoy::config::core::v3::ApiConfigSource& config_source) override;

private:
  // Validates (syntactically) the config_source by doing the PGV validation.
  absl::Status validateAdsConfig(const envoy::config::core::v3::ApiConfigSource& config_source);

  Upstream::ClusterManager& cm_;
  ProtobufMessage::ValidationContext& validation_context_;
};

} // namespace Config
} // namespace Envoy
