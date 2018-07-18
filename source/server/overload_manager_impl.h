#pragma once

#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "envoy/config/overload/v2alpha/overload.pb.validate.h"
#include "envoy/event/dispatcher.h"
#include "envoy/server/overload_manager.h"
#include "envoy/server/resource_monitor.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Server {

class OverloadActionImpl {
public:
  OverloadActionImpl(const envoy::config::overload::v2alpha::OverloadAction& config);

  // Updates the current pressure for the given resource and returns whether the action
  // has changed state.
  bool updateResourcePressure(const std::string& name, double pressure);

  // Returns whether the action is currently active or not.
  bool isActive() const;

  class Trigger {
  public:
    virtual ~Trigger() {}

    // Updates the current value of the metric and returns whether the trigger has changed state.
    virtual bool updateValue(double value) = 0;

    // Returns whether the trigger is currently fired or not.
    virtual bool isFired() const = 0;
  };
  typedef std::unique_ptr<Trigger> TriggerPtr;

private:
  std::unordered_map<std::string, TriggerPtr> triggers_;
  std::unordered_set<std::string> fired_triggers_;
};

class OverloadManagerImpl : Logger::Loggable<Logger::Id::main>, public OverloadManager {
public:
  OverloadManagerImpl(Event::Dispatcher& dispatcher,
                      const envoy::config::overload::v2alpha::OverloadManager& config);

  void start() override;

  void registerForAction(const std::string& action, Event::Dispatcher& dispatcher,
                         std::function<void(bool)> callback) override;

private:
  class Resource : public ResourceMonitor::Callbacks {
  public:
    Resource(const std::string& name, ResourceMonitorPtr monitor, OverloadManagerImpl& manager)
        : name_(name), monitor_(std::move(monitor)), manager_(manager), pending_update_(false) {}

    void onSuccess(const ResourceUsage& usage) override;
    void onFailure(const EnvoyException& error) override;

    void update();

  private:
    const std::string name_;
    ResourceMonitorPtr monitor_;
    OverloadManagerImpl& manager_;
    bool pending_update_;
  };

  struct ActionCallback {
    ActionCallback(Event::Dispatcher& dispatcher, std::function<void(bool)> callback)
        : dispatcher_(dispatcher), callback_(callback) {}
    Event::Dispatcher& dispatcher_;
    std::function<void(bool)> callback_;
  };

  void updateResourcePressure(const std::string& resource, double pressure);

  bool started_;
  Event::Dispatcher& dispatcher_;
  const std::chrono::milliseconds refresh_interval_;
  Event::TimerPtr timer_;
  std::unordered_map<std::string, Resource> resources_;
  std::unordered_map<std::string, OverloadActionImpl> actions_;
  std::unordered_multimap<std::string, std::string> resource_to_actions_;
  std::unordered_multimap<std::string, ActionCallback> action_to_callbacks_;
};

} // namespace Server
} // namespace Envoy
