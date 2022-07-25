#pragma once

#include <string>
#include <vector>

#include "envoy/config/subscription.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Upstream {

/**
 * A named helper class for handling a successful cluster configuration update from Subscription. A
 * name is used mostly for logging to differentiate between different users of the helper class.
 */
class CdsApiHelper : Logger::Loggable<Logger::Id::upstream> {
public:
  CdsApiHelper(ClusterManager& cm, std::string name) : cm_(cm), name_(std::move(name)) {}
  /**
   * onConfigUpdate handles the addition and removal of clusters by notifying the ClusterManager
   * about the cluster changes. It closely follows the onConfigUpdate API from
   * Config::SubscriptionCallbacks, with the exception of the return value documented below.
   *
   * @param added_resources clusters newly added since the previous fetch.
   * @param removed_resources names of clusters that this fetch instructed to be removed.
   * @param system_version_info aggregate response data "version", for debugging.
   * @return std::vector<std::string> a list of errors that occurred while updating the clusters.
   */
  std::vector<std::string>
  onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                 const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                 const std::string& system_version_info);
  const std::string versionInfo() const { return system_version_info_; }

private:
  ClusterManager& cm_;
  const std::string name_;
  std::string system_version_info_;
};

} // namespace Upstream
} // namespace Envoy
