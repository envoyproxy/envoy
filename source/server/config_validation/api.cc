#include "server/config_validation/api.h"

#include "server/config_validation/dispatcher.h"

namespace Envoy {
namespace Api {

ValidationImpl::ValidationImpl(std::chrono::milliseconds file_flush_interval_msec,
                               Thread::ThreadFactory& thread_factory, Stats::Store& stats_store)
    : Impl(file_flush_interval_msec, thread_factory, stats_store) {}

Event::DispatcherPtr ValidationImpl::allocateDispatcher(Event::TimeSystem& time_system) {
  return Event::DispatcherPtr{new Event::ValidationDispatcher(time_system)};
}

} // namespace Api
} // namespace Envoy
