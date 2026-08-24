#include "source/common/coroutine/any_of.h"

namespace Envoy {
namespace Coroutine {
namespace Detail {

void AnyOfStateBase::cancelAll() {
  if (finished) {
    return;
  }
  finished = true;
  if (parent_cancel) {
    parent_cancel->clearCancelCallback();
  }
  for (auto& cancel : child_cancels) {
    if (cancel && !cancel->cancelled()) {
      cancel->cancel();
    }
  }
}

void AnyOfStateBase::cancelChildrenExcept(size_t winner_index) {
  for (size_t i = 0; i < child_cancels.size(); ++i) {
    if (i != winner_index && child_cancels[i] && !child_cancels[i]->cancelled()) {
      child_cancels[i]->cancel();
    }
  }
}

} // namespace Detail
} // namespace Coroutine
} // namespace Envoy
