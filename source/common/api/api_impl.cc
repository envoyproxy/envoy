#include "common/api/api_impl.h"

#include <chrono>
#include <string>

#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"

namespace Envoy {
namespace Api {

Impl::Impl(Thread::ThreadFactory& thread_factory, Stats::Store& store,
           Event::TimeSystem& time_system, Filesystem::Instance& file_system)
    : thread_factory_(thread_factory), store_(store), time_system_(time_system),
      file_system_(file_system) {
  std::cerr << "Impl() time_system " << &time_system << "\n";
}

Event::DispatcherPtr Impl::allocateDispatcher() {
  std::cerr << "allocateDispatcher with TimeSystem " << &time_system_;
  return std::make_unique<Event::DispatcherImpl>(*this, time_system_);
}

Event::DispatcherPtr Impl::allocateDispatcher(Buffer::WatermarkFactoryPtr&& factory) {
  return std::make_unique<Event::DispatcherImpl>(std::move(factory), *this, time_system_);
}

} // namespace Api
} // namespace Envoy
