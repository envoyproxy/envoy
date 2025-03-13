#include "source/extensions/resource_monitors/injected_resource/injected_resource_monitor.h"

#include "envoy/extensions/resource_monitors/injected_resource/v3/injected_resource.pb.h"

#include "source/common/common/assert.h"

#include "absl/strings/numbers.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace InjectedResourceMonitor {

InjectedResourceMonitor::InjectedResourceMonitor(
    const envoy::extensions::resource_monitors::injected_resource::v3::InjectedResourceConfig&
        config,
    Server::Configuration::ResourceMonitorFactoryContext& context)
    : filename_(config.filename()),
      watcher_(context.mainThreadDispatcher().createFilesystemWatcher()), api_(context.api()) {
  THROW_IF_NOT_OK(
      watcher_->addWatch(filename_, Filesystem::Watcher::Events::MovedTo, [this](uint32_t) {
        onFileChanged();
        return absl::OkStatus();
      }));
}

void InjectedResourceMonitor::onFileChanged() { file_changed_ = true; }

void InjectedResourceMonitor::updateResourceUsage(Server::ResourceUpdateCallbacks& callbacks) {
  if (file_changed_) {
    file_changed_ = false;
    TRY_ASSERT_MAIN_THREAD {
      auto file_or_error = api_.fileSystem().fileReadToEnd(filename_);
      THROW_IF_NOT_OK_REF(file_or_error.status());
      const std::string contents = file_or_error.value();
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
    }
    END_TRY
    catch (const EnvoyException& error) {
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
