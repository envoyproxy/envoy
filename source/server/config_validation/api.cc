#include "server/config_validation/api.h"

#include "server/config_validation/dispatcher.h"

namespace Envoy {
namespace Api {

ValidationImpl::ValidationImpl(std::chrono::milliseconds file_flush_interval_msec,
                               Thread::ThreadFactory& thread_factory, Stats::Store& stats_store,
                               Event::TimeSystem& time_system)
    : Impl(file_flush_interval_msec, thread_factory, stats_store, time_system) {}

Event::DispatcherPtr ValidationImpl::allocateDispatcher() {
  return Event::DispatcherPtr{new Event::ValidationDispatcher(*this)};
}

} // namespace Api
} // namespace Envoy
