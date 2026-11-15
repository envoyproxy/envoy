#pragma once

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

/**
 * Tracks the envoy_dynamic_module_on_program_init calls of the loaded modules across threads, so
 * that a load waits while another load runs the init function of the same module, while the loads
 * of other modules do not wait.
 *
 * The init state of a shared object is forgotten only when a load opens a shared object at its
 * handle again. A shared object that is loaded in another way at the handle of one that was
 * unloaded, such as a dependency of another shared object, keeps the init state of the unloaded
 * one.
 */
class ModuleInitRegistry {
public:
  static ModuleInitRegistry& get();

  /**
   * The mutex that serializes the opening of shared objects with the changes of their init state,
   * since a shared object that is unloaded can be replaced by another one at the same handle.
   */
  absl::Mutex& mutex() ABSL_LOCK_RETURNED(mutex_) { return mutex_; }

  /**
   * Waits while another load runs the init function of the shared object.
   * @param handle the handle of the shared object.
   */
  void waitForSharedObjectInit(const void* handle) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /**
   * Waits while another load runs the init function of the shared object, and returns whether this
   * load must run it, which it then does until finishSharedObjectInit. The init function of a
   * shared object runs again only if it failed.
   * @param handle the handle of the shared object.
   */
  bool startSharedObjectInit(const void* handle) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /**
   * Records the outcome of the init function that startSharedObjectInit returned true for.
   * @param handle the handle of the shared object.
   * @param succeeded whether the init function succeeded.
   */
  void finishSharedObjectInit(const void* handle, bool succeeded) ABSL_LOCKS_EXCLUDED(mutex_);

  /**
   * Forgets the init state of a shared object that was unloaded, since another shared object can
   * get its handle. The init state is kept while a load runs the init function, since that load
   * keeps the shared object loaded, even if another load reached it by another path.
   * @param handle the handle of the shared object.
   */
  void forgetSharedObject(const void* handle) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  /**
   * Waits while another load runs the init function of the statically linked module, which this
   * load then runs until finishStaticModuleInit.
   * @param name the name of the statically linked module.
   */
  void startStaticModuleInit(absl::string_view name) ABSL_LOCKS_EXCLUDED(mutex_);

  /**
   * Records that the init function that startStaticModuleInit was called for returned.
   * @param name the name of the statically linked module.
   */
  void finishStaticModuleInit(absl::string_view name) ABSL_LOCKS_EXCLUDED(mutex_);

private:
  struct SharedObjectInit {
    bool running = false;
    bool succeeded = false;
  };

  absl::Mutex mutex_;
  absl::flat_hash_map<const void*, SharedObjectInit> shared_objects_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_set<std::string> running_static_modules_ ABSL_GUARDED_BY(mutex_);
};

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
