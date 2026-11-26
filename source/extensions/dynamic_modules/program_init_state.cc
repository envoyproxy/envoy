#include "source/extensions/dynamic_modules/program_init_state.h"

#include "source/common/common/macros.h"

#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

namespace {

struct ProgramInitState {
  absl::Mutex mutex;
  absl::flat_hash_set<const void*> succeeded ABSL_GUARDED_BY(mutex);
};

ProgramInitState& programInitState() { MUTABLE_CONSTRUCT_ON_FIRST_USE(ProgramInitState); }

} // namespace

bool programInitSucceeded(const void* handle) {
  ProgramInitState& state = programInitState();
  absl::MutexLock lock(state.mutex);
  return state.succeeded.contains(handle);
}

void setProgramInitSucceeded(const void* handle, bool succeeded) {
  ProgramInitState& state = programInitState();
  absl::MutexLock lock(state.mutex);
  if (succeeded) {
    state.succeeded.insert(handle);
  } else {
    state.succeeded.erase(handle);
  }
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
