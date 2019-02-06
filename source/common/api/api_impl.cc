#include "common/api/api_impl.h"

#include <chrono>
#include <string>

#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"

namespace Envoy {
namespace Api {

Impl::Impl(std::chrono::milliseconds file_flush_interval_msec,
           Thread::ThreadFactory& thread_factory, Stats::Store& stats_store,
           Event::TimeSystem& time_system)
    : thread_factory_(thread_factory),
      file_system_(file_flush_interval_msec, thread_factory, stats_store),
      time_system_(time_system) {}

Event::DispatcherPtr Impl::allocateDispatcher() {
  return std::make_unique<Event::DispatcherImpl>(*this);
}

} // namespace Api
} // namespace Envoy
