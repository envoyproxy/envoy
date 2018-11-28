#include "common/common/thread.h"

#include <functional>

#include "common/common/thread_impl.h"

namespace Envoy {
namespace Thread {

ThreadPtr ThreadFactoryImpl::createThread(std::function<void()> thread_routine) {
  return std::make_unique<ThreadImpl>(thread_routine);
}

} // namespace Thread
} // namespace Envoy
