#pragma once

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/http/context_impl.h"
#include "common/upstream/cluster_manager_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Config-validation-only implementation of ClusterManagerFactory, which creates
 * ValidationClusterManagers. It also creates, but never returns, CdsApiImpls.
 */
class ValidationClusterManagerFactory : public ProdClusterManagerFactory {
public:
  using ProdClusterManagerFactory::ProdClusterManagerFactory;

  // Delegates to ProdClusterManagerFactory::createCds, but discards the result and returns nullptr
  // unconditionally.
  CdsApiPtr createCds(const envoy::config::core::v3::ConfigSource& cds_config,
                      ClusterManager& cm) override;
};

} // namespace Upstream
} // namespace Envoy
