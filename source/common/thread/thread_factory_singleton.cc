#include "envoy/thread/thread.h"

namespace Envoy {
namespace Thread {

ThreadFactory* ThreadFactorySingleton::thread_factory_{nullptr};

} // namespace Thread
} // namespace Envoy
