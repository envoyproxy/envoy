#pragma once

#include "envoy/api/api.h"
#include "envoy/extensions/resource_monitors/injected_resource/v3/injected_resource.pb.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/server/resource_monitor.h"
#include "envoy/server/resource_monitor_config.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace InjectedResourceMonitor {

/**
 * A monitor for an injected resource. The resource pressure is read from a text file
 * specified in the config, which must contain a floating-point number in the range
 * [0..1] and be updated atomically by a symbolic link swap.
 * This is intended primarily for integration tests to force Envoy into an overloaded state.
 */
class InjectedResourceMonitor : public Server::ResourceMonitor {
public:
  InjectedResourceMonitor(
      const envoy::extensions::resource_monitors::injected_resource::v3::InjectedResourceConfig&
          config,
      Server::Configuration::ResourceMonitorFactoryContext& context);

  // Server::ResourceMonitor
  void updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) override;

protected:
  virtual void onFileChanged();

private:
  const std::string filename_;
  bool file_changed_{true};
  Filesystem::WatcherPtr watcher_;
  absl::optional<double> pressure_;
  absl::optional<EnvoyException> error_;
  Api::Api& api_;
};

} // namespace InjectedResourceMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
