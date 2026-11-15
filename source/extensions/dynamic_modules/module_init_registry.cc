#include "source/extensions/dynamic_modules/module_init_registry.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

ModuleInitRegistry& ModuleInitRegistry::get() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(ModuleInitRegistry);
}

void ModuleInitRegistry::waitForSharedObjectInit(const void* handle) {
  const auto not_running = [this, handle]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    const auto it = shared_objects_.find(handle);
    return it == shared_objects_.end() || !it->second.running;
  };
  mutex_.Await(absl::Condition(&not_running));
}

bool ModuleInitRegistry::startSharedObjectInit(const void* handle) {
  waitForSharedObjectInit(handle);
  SharedObjectInit& init = shared_objects_[handle];
  if (init.succeeded) {
    return false;
  }
  init.running = true;
  return true;
}

void ModuleInitRegistry::finishSharedObjectInit(const void* handle, bool succeeded) {
  absl::MutexLock lock(mutex_);
  SharedObjectInit& init = shared_objects_[handle];
  init.running = false;
  init.succeeded = succeeded;
}

void ModuleInitRegistry::forgetSharedObject(const void* handle) {
  const auto it = shared_objects_.find(handle);
  if (it != shared_objects_.end() && !it->second.running) {
    shared_objects_.erase(it);
  }
}

void ModuleInitRegistry::startStaticModuleInit(absl::string_view name) {
  absl::MutexLock lock(mutex_);
  const auto not_running = [this, name]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
    return !running_static_modules_.contains(name);
  };
  mutex_.Await(absl::Condition(&not_running));
  running_static_modules_.emplace(name);
}

void ModuleInitRegistry::finishStaticModuleInit(absl::string_view name) {
  absl::MutexLock lock(mutex_);
  running_static_modules_.erase(name);
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
