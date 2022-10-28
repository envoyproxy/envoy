#include "source/extensions/common/async_files/async_file_context_base.h"

#include <functional>
#include <memory>
#include <utility>

#include "source/extensions/common/async_files/async_file_action.h"
#include "source/extensions/common/async_files/async_file_manager.h"

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

AsyncFileContextBase::AsyncFileContextBase(AsyncFileManager& manager) : manager_(manager) {}

CancelFunction AsyncFileContextBase::enqueue(std::shared_ptr<AsyncFileAction> action) {
  return manager_.enqueue(std::move(action));
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
