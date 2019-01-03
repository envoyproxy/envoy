#include "envoy/thread/thread.h"

namespace Envoy {
namespace Thread {

#ifndef NDEBUG
// This static singleton is only defined for debug builds.
ThreadFactory* ThreadFactorySingleton::thread_factory_{nullptr};
#endif

} // namespace Thread
} // namespace Envoy
