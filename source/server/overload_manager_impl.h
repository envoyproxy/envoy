#pragma once

#include <chrono>
#include <thread>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/config/overload/v3/overload.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/overload_manager.h"
#include "envoy/server/resource_monitor.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/logger.h"

#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"

namespace Envoy {
namespace Server {

class OverloadAction {
public:
  OverloadAction(const envoy::config::overload::v3::OverloadAction& config,
                 Stats::Scope& stats_scope);

  // Updates the current pressure for the given resource and returns whether the action
  // has changed state.
  bool updateResourcePressure(const std::string& name, double pressure);

  // Returns the current action state, which is the max state across all registered triggers.
  OverloadActionState getState() const;

  class Trigger {
  public:
    virtual ~Trigger() = default;

    // Updates the current value of the metric and returns whether the trigger has changed state.
    virtual bool updateValue(double value) PURE;

    // Returns the action state for the trigger.
    virtual OverloadActionState actionState() const PURE;
  };
  using TriggerPtr = std::unique_ptr<Trigger>;

private:
  absl::node_hash_map<std::string, TriggerPtr> triggers_;
  OverloadActionState state_;
  Stats::Gauge& active_gauge_;
  Stats::Gauge& scale_percent_gauge_;
};

class OverloadManagerImpl : Logger::Loggable<Logger::Id::main>, public OverloadManager {
public:
  OverloadManagerImpl(Event::Dispatcher& dispatcher, Stats::Scope& stats_scope,
                      ThreadLocal::SlotAllocator& slot_allocator,
                      const envoy::config::overload::v3::OverloadManager& config,
                      ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api);

  // Server::OverloadManager
  void start() override;
  bool registerForAction(const std::string& action, Event::Dispatcher& dispatcher,
                         OverloadActionCb callback) override;
  ThreadLocalOverloadState& getThreadLocalOverloadState() override;

  // Stop the overload manager timer and wait for any pending resource updates to complete.
  // After this returns, overload manager clients should not receive any more callbacks
  // about overload state changes.
  void stop();
  // Updates stats within specific resource monitor.
  // Currently only integer values for stats are supported.
  // todo(nezdolik) switch to ThreadLocalResourceStat struct
  bool updateResourceStats(const std::thread::id thread_id, const std::string& resource,
                           const std::string& stat_name, const uint64_t value);

private:
  class Resource : public ResourceMonitor::Callbacks {
  public:
    Resource(const std::string& name, ResourceMonitorPtr monitor, OverloadManagerImpl& manager,
             Stats::Scope& stats_scope);

    // ResourceMonitor::Callbacks
    void onSuccess(const ResourceUsage& usage) override;
    void onFailure(const EnvoyException& error) override;

    void update();
    bool updateResourceStats(const std::thread::id thread_id, const std::string& stat_name,
                             uint64_t value);

  private:
    const std::string name_;
    ResourceMonitorPtr monitor_;
    OverloadManagerImpl& manager_;
    bool pending_update_;
    Stats::Gauge& pressure_gauge_;
    Stats::Counter& failed_updates_counter_;
    Stats::Counter& skipped_updates_counter_;
  };

  struct ActionCallback {
    ActionCallback(Event::Dispatcher& dispatcher, OverloadActionCb callback)
        : dispatcher_(dispatcher), callback_(callback) {}
    Event::Dispatcher& dispatcher_;
    OverloadActionCb callback_;
  };

  // struct ThreadLocalResourceStat {
  //   std::thread::id thread_id;
  //   std::string resource_id;
  //   std::string stat_name;
  //   uint64_t value;
  // };

  // using ThreadLocalResourceStatPtr = std::unique_ptr<ThreadLocalResourceStat>;

  void updateResourcePressure(const std::string& resource, double pressure);

  bool started_;
  Event::Dispatcher& dispatcher_;
  ThreadLocal::SlotPtr tls_;
  const std::chrono::milliseconds refresh_interval_;
  Event::TimerPtr timer_;
  absl::node_hash_map<std::string, Resource> resources_;
  absl::node_hash_map<std::string, OverloadAction> actions_;

  using ResourceToActionMap = std::unordered_multimap<std::string, std::string>;
  ResourceToActionMap resource_to_actions_;

  using ActionToCallbackMap = std::unordered_multimap<std::string, ActionCallback>;
  ActionToCallbackMap action_to_callbacks_;
};

} // namespace Server
} // namespace Envoy
