#include "source/extensions/bootstrap/dynamic_modules/writer_shared_state.h"

#include <chrono>

#include "envoy/singleton/manager.h"

#include "source/common/config/dependent_type_urls.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

namespace {

// How long the passes of a burst may hold back the requests of the dependent xDS types.
constexpr std::chrono::milliseconds MaxDependentTypePause{1000};

} // namespace

SINGLETON_MANAGER_REGISTRATION(dynamic_modules_bootstrap_writer_shared_state);

std::shared_ptr<WriterSharedState>
WriterSharedState::get(Server::Configuration::ServerFactoryContext& context) {
  return context.singletonManager().getTyped<WriterSharedState>(
      SINGLETON_MANAGER_REGISTERED_NAME(dynamic_modules_bootstrap_writer_shared_state),
      [] { return std::make_shared<WriterSharedState>(); });
}

void WriterSharedState::pauseDependentTypes(absl::string_view resource_type_url,
                                            Config::XdsManager& xds_manager, MonotonicTime now) {
  if (dependent_type_pauses_.empty()) {
    dependent_type_pause_start_ = now;
  }
  auto [it, inserted] = dependent_type_pauses_.try_emplace(resource_type_url);
  if (inserted) {
    it->second = xds_manager.pause(Config::dependentTypeUrls(resource_type_url));
  }
}

void WriterSharedState::onApplyPassDone(const void* writer, bool burst_applied, MonotonicTime now) {
  if (burst_applied) {
    writers_applying_.erase(writer);
  } else {
    writers_applying_.insert(writer);
  }
  // Modules that write all the time must not hold back these requests for long.
  if (writers_applying_.empty() || now - dependent_type_pause_start_ >= MaxDependentTypePause) {
    dependent_type_pauses_.clear();
  }
}

void WriterSharedState::onShutdown(const void* writer) {
  writers_applying_.erase(writer);
  if (writers_applying_.empty()) {
    dependent_type_pauses_.clear();
  }
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
