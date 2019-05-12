#pragma once

#include "envoy/server/config_tracker.h"

#include "common/common/assert.h"
#include "common/common/macros.h"

namespace Envoy {
namespace Server {

/**
 * Implementation of ConfigTracker.
 */
class ConfigTrackerImpl : public ConfigTracker {
public:
  EntryOwnerPtr add(const std::string& key, Cb cb) override;
  const CbsMap& getCallbacksMap() const override;
  void addOrUpdateControlPlaneConfig(const std::string& service,
                                     ControlPlaneConfigPtr control_plane_info) override;
  ControlPlaneConfigPtr getControlPlaneConfig(const std::string& service) const override;
  const ControlPlaneConfigMap& getControlPlaneConfigMap() const override;

private:
  std::shared_ptr<CbsMap> map_{std::make_shared<CbsMap>()};
  std::shared_ptr<ControlPlaneConfigMap> control_plane_config_{
      std::make_shared<ControlPlaneConfigMap>()};

  class EntryOwnerImpl : public ConfigTracker::EntryOwner {
  public:
    EntryOwnerImpl(const std::shared_ptr<CbsMap>& map, const std::string& key);
    ~EntryOwnerImpl();

  private:
    std::shared_ptr<CbsMap> map_;
    std::string key_;
  };
};

} // namespace Server
} // namespace Envoy
