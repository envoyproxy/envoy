#pragma once

#include "envoy/api/api.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Upstream {

class ProdClusterInfoFactory : public ClusterInfoFactory, Logger::Loggable<Logger::Id::upstream> {
public:
  ClusterInfoConstSharedPtr createClusterInfo(const CreateClusterInfoParams& params) override;
};

} // namespace Upstream
} // namespace Envoy
