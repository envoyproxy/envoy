#pragma once

#include <string>
#include <vector>

#include "envoy/config/subscription.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

class CdsApiHelper : Logger::Loggable<Logger::Id::upstream> {
public:
  CdsApiHelper(ClusterManager& cm, std::string name) : cm_(cm), name_(std::move(name)) {}
  std::vector<std::string>
  onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                 const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                 const std::string& system_version_info);
  const std::string versionInfo() const { return system_version_info_; }

private:
  ClusterManager& cm_;
  std::string name_;
  std::string system_version_info_;
};

} // namespace Upstream
} // namespace Envoy
