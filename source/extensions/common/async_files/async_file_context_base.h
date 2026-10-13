#pragma once

#include <memory>

#include "envoy/event/dispatcher.h"

#include "source/extensions/common/async_files/async_file_handle.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

class AsyncFileAction;
class AsyncFileManager;

// The base implementation of an AsyncFileHandle that uses the manager thread pool - includes
// all the parts that interact with the manager's thread pool.
// See AsyncFileContextThreadPool for an example implementation of the actions on top of this base.
class AsyncFileContextBase : public AsyncFileContext {
public:
  AsyncFileManager& manager() const { return manager_; }

protected:
  // Queue up an action with the AsyncFileManager.
  CancelFunction enqueue(Event::Dispatcher* dispatcher, std::unique_ptr<AsyncFileAction> action);

  explicit AsyncFileContextBase(AsyncFileManager& manager);

protected:
  AsyncFileManager& manager_;
  AsyncFileHandle handle() { return shared_from_this(); }
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
