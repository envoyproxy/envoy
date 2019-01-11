#include "extensions/resource_monitors/injected_resource/injected_resource_monitor.h"

#include "common/common/assert.h"
#include "common/filesystem/filesystem_impl.h"

#include "absl/strings/numbers.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace InjectedResourceMonitor {

InjectedResourceMonitor::InjectedResourceMonitor(
    const envoy::config::resource_monitor::injected_resource::v2alpha::InjectedResourceConfig&
        config,
    Server::Configuration::ResourceMonitorFactoryContext& context)
    : filename_(config.filename()), file_changed_(true),
      watcher_(context.dispatcher().createFilesystemWatcher()) {
  watcher_->addWatch(filename_, Filesystem::Watcher::Events::MovedTo,
                     [this](uint32_t) { onFileChanged(); });
}

void InjectedResourceMonitor::onFileChanged() { file_changed_ = true; }

void InjectedResourceMonitor::updateResourceUsage(Server::ResourceMonitor::Callbacks& callbacks) {
  if (file_changed_) {
    file_changed_ = false;
    try {
      const std::string contents = Filesystem::fileReadToEnd(filename_);
      double pressure;
      if (absl::SimpleAtod(contents, &pressure)) {
        if (pressure < 0 || pressure > 1) {
          throw EnvoyException("pressure out of range");
        }
        pressure_ = pressure;
        error_.reset();
      } else {
        throw EnvoyException("failed to parse injected resource pressure");
      }
    } catch (const EnvoyException& error) {
      error_ = error;
      pressure_.reset();
    }
  }

  ASSERT(pressure_.has_value() != error_.has_value());
  if (pressure_.has_value()) {
    callbacks.onSuccess({*pressure_});
  } else {
    callbacks.onFailure(*error_);
  }
}

} // namespace InjectedResourceMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
