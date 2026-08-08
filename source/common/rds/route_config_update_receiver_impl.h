#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/rds/config_traits.h"
#include "envoy/rds/route_config_update_receiver.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/common/init/manager_impl.h"
#include "source/common/init/watcher_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Rds {

/**
 * Helper that warms up the resources owned by a newly built route configuration and notifies an
 * observer once they are ready.
 *
 * Every update gets its own independent init manager, so that the resources of the new route
 * configuration can be warmed up without interfering with the route configuration that is currently
 * published. The init manager is dropped again once the new route configuration is warmed up, so
 * there is no init manager in between updates.
 *
 * At most one update warms up at a time. An update that arrives while another one is still warming
 * up supersedes it: the superseded update is abandoned and never published.
 */
class ConfigWarmer : protected Logger::Loggable<Logger::Id::rds> {
public:
  ConfigWarmer(std::function<void()> on_warmed_callback = []() {})
      : on_warmed_callback_(on_warmed_callback) {}

  /**
   * Sets the observer that is notified once an update is warmed up.
   * @param observer supplies the observer. This should have a lifetime that is at least as long as
   * the lifetime of this warmer.
   */
  void setObserver(RouteConfigUpdateObserver& observer) { observer_.emplace(observer); }

  /**
   * Creates the init manager that the resources of the update that is about to be built should warm
   * up with. The caller keeps it local until the update is known to be applied, so that an update
   * that turns out to be a no-op leaves a previous update that is still warming up alone.
   * @param update_id identifies the update in the names of the init manager and its watcher.
   */
  std::unique_ptr<Init::ManagerImpl> createInitManager(absl::string_view update_id) {
    return std::make_unique<Init::ManagerImpl>(fmt::format("{} update-init-manager", update_id));
  }

  /**
   * Takes ownership of the init manager created by createInitManager(), without starting to warm
   * it up yet. If a previous update is still warming up it is abandoned here, as this update
   * supersedes it. Warming only starts when startWarming() is called, so that the caller can
   * finish applying the update before anything can be published.
   * @param init_manager supplies the init manager of the update that is being applied.
   * @param update_id identifies the update in the names of the init manager and its watcher.
   */
  void updateInitManager(std::unique_ptr<Init::ManagerImpl> init_manager,
                         absl::string_view update_id);

  /**
   * Starts warming up the init manager installed by updateInitManager(). The observer is notified
   * once everything that registered to that init manager is ready. Note that this may happen
   * before this method returns, i.e. synchronously, if there is nothing to warm up.
   */
  void startWarming();

  bool warming() const { return init_manager_ != nullptr; }

private:
  // Called when everything that registered to the init manager of the update that is warming up is
  // ready. Drops the per-update init manager and notifies the observer.
  void onWarmed();

  OptRef<RouteConfigUpdateObserver> observer_;
  // Init manager that is used to warm up the resources owned by the route configuration of the
  // update that is warming up. Null if no update is warming up.
  std::unique_ptr<Init::ManagerImpl> init_manager_;
  // The init manager of the update that was warmed up last. It can't be destroyed from inside its
  // own readiness callback, see onWarmed(), so it is moved aside there and destroyed once the next
  // update replaces it.
  std::unique_ptr<Init::ManagerImpl> deferred_delete_init_manager_;
  // Watcher that init_manager_ notifies once everything it warms up is ready.
  std::unique_ptr<Init::WatcherImpl> init_watcher_;
  // Identifies the update that is warming up in logs.
  std::string update_id_;
  std::function<void()> on_warmed_callback_;
};

// The state of one route configuration: the proto it was built from, the parsed configuration and
// the bookkeeping that goes with them. A receiver keeps one for the published route configuration
// and one for the update that is warming up, if any.
struct RouteConfigState {
  ProtobufTypes::MessagePtr route_config_proto_;
  ConfigConstSharedPtr config_;
  std::optional<RouteConfigProvider::ConfigInfo> config_info_;
  uint64_t last_config_hash_{0ull};
  SystemTime last_updated_;
};

class RouteConfigUpdateReceiverImpl : public RouteConfigUpdateReceiver {
public:
  RouteConfigUpdateReceiverImpl(ConfigTraits& config_traits, ProtoTraits& proto_traits,
                                Server::Configuration::ServerFactoryContext& factory_context);

  uint64_t getHash(const Protobuf::Message& rc) const { return MessageUtil::hash(rc); }
  bool checkHash(uint64_t new_hash) const {
    if (warming_state_.route_config_proto_ != nullptr) {
      // There is an update that is warming up. Check if the new update is the same as that one, and
      // if so, ignore it.
      if (new_hash == warming_state_.last_config_hash_) {
        return false;
      }
    } else {
      // There is no update that is warming up. Check if the new update is the same as the current
      // one, and if so, ignore it.
      if (new_hash == current_state_.last_config_hash_) {
        return false;
      }
    }
    return true;
  }
  // Builds a new route configuration and installs the init manager that its resources warm up
  // with, but doesn't warm anything up or publish anything yet. The caller finishes applying the
  // update and then calls startWarming(), which eventually publishes it.
  void updateConfig(std::unique_ptr<Protobuf::Message> route_config_proto, uint64_t hash,
                    absl::string_view version_info);
  // Warms up the route configuration built by the last updateConfig() call and publishes it once
  // it's ready. Note that this may happen before this method returns, i.e. synchronously, if there
  // is nothing to warm up.
  void startWarming() { warmer_.startWarming(); }

  // RouteConfigUpdateReceiver
  bool onRdsUpdate(const Protobuf::Message& rc, const std::string& version_info) override;
  void setObserver(RouteConfigUpdateObserver& observer) override { warmer_.setObserver(observer); }
  bool configWarming() const override { return warmer_.warming(); }

  uint64_t configHash() const override { return current_state_.last_config_hash_; }
  const std::optional<RouteConfigProvider::ConfigInfo>& configInfo() const override {
    return current_state_.config_info_;
  }
  const Protobuf::Message& protobufConfiguration() const override {
    return *current_state_.route_config_proto_;
  }
  ConfigConstSharedPtr parsedConfiguration() const override { return current_state_.config_; }
  SystemTime lastUpdated() const override { return current_state_.last_updated_; }

  ConfigWarmer& warmer() { return warmer_; }
  const ConfigWarmer& warmer() const { return warmer_; }
  const Protobuf::Message& latestProtobufConfiguration() const {
    return warming_state_.route_config_proto_ ? *warming_state_.route_config_proto_
                                              : *current_state_.route_config_proto_;
  }

private:
  void onConfigWarmed();

  ConfigTraits& config_traits_;
  ProtoTraits& proto_traits_;
  Server::Configuration::ServerFactoryContext& factory_context_;
  TimeSource& time_source_;
  ConfigWarmer warmer_;
  RouteConfigState current_state_;
  RouteConfigState warming_state_;
};

} // namespace Rds
} // namespace Envoy
