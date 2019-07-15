#pragma once

#include <set>
#include <string>

#include "envoy/config/watch_map.h"

namespace Envoy {
namespace Config {

class WatchImpl : public Watch {
public:
  WatchImpl(WatchMap& parent_map, SubscriptionCallbacks& callbacks)
      : parent_map_(parent_map), callbacks_(callbacks) {}
  ~WatchImpl() override { parent_map_.removeWatch(this); }

  AddedRemoved updateWatchInterest(const std::set<std::string>& update_to_these_names) override;

private:
  // SubscriptionCallbacks (all passthroughs to callbacks_)
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& system_version_info) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any&) override;

  WatchMap& parent_map_;
  SubscriptionCallbacks& callbacks_;
  std::set<std::string> resource_names_; // must be sorted set, for set_difference.
};

} // namespace Config
} // namespace Envoy
