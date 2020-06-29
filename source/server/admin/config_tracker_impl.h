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

private:
  CbsMapSharedPtr map_{std::make_shared<CbsMap>()};

  class EntryOwnerImpl : public ConfigTracker::EntryOwner {
  public:
    EntryOwnerImpl(const CbsMapSharedPtr& map, const std::string& key);
    ~EntryOwnerImpl() override;

  private:
    CbsMapSharedPtr map_;
    std::string key_;
  };
};

} // namespace Server
} // namespace Envoy
