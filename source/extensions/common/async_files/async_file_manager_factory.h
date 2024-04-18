#pragma once

#include "envoy/extensions/common/async_files/v3/async_file_manager.pb.h"
#include "envoy/singleton/instance.h"
#include "envoy/singleton/manager.h"

#include "source/extensions/common/async_files/async_file_manager.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

// A singleton factory for instantiating AsyncFileManagers.
//
// This ensures that each distinct id from an AsyncFileManagerConfig represents
// only one AsyncFileManager.
class AsyncFileManagerFactory : public Singleton::Instance {
public:
  // Returns an AsyncFileManagerFactory.
  //
  // Destroying this factory destroys the mapping from ids to AsyncFileManagers. Make sure
  // an instance of the returned shared_ptr remains in scope for the lifetime of the managers,
  // to avoid accidentally generating duplicate managers.
  //
  // Specifically, the singleton manager *does not* keep a reference to the returned singleton
  // - the factory persists only as long as there is a live reference to it.
  static std::shared_ptr<AsyncFileManagerFactory> singleton(Singleton::Manager* singleton_manager);
  virtual std::shared_ptr<AsyncFileManager> getAsyncFileManager(
      const envoy::extensions::common::async_files::v3::AsyncFileManagerConfig& config,
      Api::OsSysCalls* substitute_posix_file_operations = nullptr) PURE;
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
