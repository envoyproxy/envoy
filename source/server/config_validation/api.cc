#include "server/config_validation/api.h"

#include "common/common/assert.h"

#include "server/config_validation/dispatcher.h"

namespace Envoy {
namespace Api {

ValidationImpl::ValidationImpl(Thread::ThreadFactory& thread_factory, Stats::Store& stats_store,
                               Event::TimeSystem& time_system, Filesystem::Instance& file_system)
    : Impl(thread_factory, stats_store, time_system, file_system), time_system_(time_system) {}

Event::DispatcherPtr ValidationImpl::allocateDispatcher() {
  return Event::DispatcherPtr{new Event::ValidationDispatcher(*this, time_system_)};
}

Event::DispatcherPtr ValidationImpl::allocateDispatcher(Buffer::WatermarkFactoryPtr&&) {
  NOT_REACHED_GCOVR_EXCL_LINE;
}

} // namespace Api
} // namespace Envoy
