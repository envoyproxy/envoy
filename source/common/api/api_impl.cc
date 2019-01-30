#include "common/api/api_impl.h"

#include <chrono>
#include <string>

#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"

namespace Envoy {
namespace Api {

Impl::Impl(std::chrono::milliseconds file_flush_interval_msec,
           Thread::ThreadFactory& thread_factory, Stats::Store& stats_store)
    : thread_factory_(thread_factory),
      file_system_(file_flush_interval_msec, thread_factory, stats_store) {}

Event::DispatcherPtr Impl::allocateDispatcher(Event::TimeSystem& time_system) {
  return std::make_unique<Event::DispatcherImpl>(time_system, *this);
}

Thread::ThreadFactory& Impl::threadFactory() { return thread_factory_; }

Filesystem::Instance& Impl::fileSystem() { return file_system_; }

} // namespace Api
} // namespace Envoy
