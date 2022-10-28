#include "source/extensions/common/async_files/async_file_action.h"

#include <thread>

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

void AsyncFileAction::cancel() {
  auto previousState = state_.exchange(State::Cancelled);
  if (previousState == State::InCallback) {
    // A gentle spin-lock. This situation should be rare, and callbacks are
    // supposed to be quick, so we don't need a real lock here.
    while (state_.load() != State::Done) {
      std::this_thread::yield();
    }
  } else if (previousState == State::Done) {
    state_.store(State::Done);
  }
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
