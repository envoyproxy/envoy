#include "server/config_validation/api.h"

#include "server/config_validation/dispatcher.h"

namespace Envoy {
namespace Api {

ValidationImpl::ValidationImpl(Thread::ThreadFactory& thread_factory, Stats::Store& stats_store,
                               Event::TimeSystem& time_system)
    : Impl(thread_factory, stats_store, time_system) {}

Event::DispatcherPtr ValidationImpl::allocateDispatcher() {
  return Event::DispatcherPtr{new Event::ValidationDispatcher(*this)};
}

} // namespace Api
} // namespace Envoy
