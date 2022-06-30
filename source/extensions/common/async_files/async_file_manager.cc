#include "source/extensions/common/async_files/async_file_manager.h"

#include <memory>
#include <vector>

#include "source/common/common/macros.h"
#include "source/extensions/common/async_files/async_file_action.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

namespace {
class ActionWhenReady : public AsyncFileActionWithResult<absl::Status> {
public:
  explicit ActionWhenReady(std::function<void(absl::Status)> on_complete)
      : AsyncFileActionWithResult<absl::Status>(on_complete) {}

  absl::Status executeImpl() override { return absl::OkStatus(); }
};
} // namespace

CancelFunction AsyncFileManager::whenReady(std::function<void(absl::Status)> on_complete) {
  return enqueue(std::make_shared<ActionWhenReady>(std::move(on_complete)));
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
