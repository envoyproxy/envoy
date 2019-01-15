#include "server/config_validation/api.h"

#include "server/config_validation/dispatcher.h"

namespace Envoy {
namespace Api {

ValidationImpl::ValidationImpl(std::chrono::milliseconds file_flush_interval_msec,
                               Thread::ThreadFactory& thread_factory, Stats::Store& stats_store,
                               Filesystem::Instance& file_system)
    : Impl(file_flush_interval_msec, thread_factory, stats_store, file_system) {}

Event::DispatcherPtr ValidationImpl::allocateDispatcher(Event::TimeSystem& time_system) {
  return Event::DispatcherPtr{new Event::ValidationDispatcher(time_system, *this)};
}

} // namespace Api
} // namespace Envoy
