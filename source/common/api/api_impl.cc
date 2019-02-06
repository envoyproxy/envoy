#include "common/api/api_impl.h"

#include <chrono>
#include <string>

#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"

namespace Envoy {
namespace Api {

Impl::Impl(Thread::ThreadFactory& thread_factory, Event::TimeSystem& time_system)
    : thread_factory_(thread_factory), time_system_(time_system) {}

Event::DispatcherPtr Impl::allocateDispatcher() {
  return std::make_unique<Event::DispatcherImpl>(*this);
}

} // namespace Api
} // namespace Envoy
