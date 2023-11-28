#include "source/server/config_validation/api.h"

#include "source/common/common/assert.h"
#include "source/server/config_validation/dispatcher.h"

namespace Envoy {
namespace Api {

ValidationImpl::ValidationImpl(Thread::ThreadFactory& thread_factory, Stats::Store& stats_store,
                               Event::TimeSystem& time_system, Filesystem::Instance& file_system,
                               Random::RandomGenerator& random_generator,
                               const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                               const ProcessContextOptRef& process_context)
    : Impl(thread_factory, stats_store, time_system, file_system, random_generator, bootstrap,
           process_context),
      time_system_(time_system) {}

Event::DispatcherPtr ValidationImpl::allocateDispatcher(const std::string& name) {
  return Event::DispatcherPtr{new Event::ValidationDispatcher(name, *this, time_system_)};
}

Event::DispatcherPtr
ValidationImpl::allocateDispatcher(const std::string&,
                                   const Event::ScaledRangeTimerManagerFactory&) {
  PANIC("not implemented");
}

Event::DispatcherPtr ValidationImpl::allocateDispatcher(const std::string&,
                                                        Buffer::WatermarkFactoryPtr&&) {
  PANIC("not implemented");
}

} // namespace Api
} // namespace Envoy
