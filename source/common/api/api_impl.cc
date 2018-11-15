#include "common/api/api_impl.h"

#include <chrono>
#include <string>

#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"
#include "common/filesystem/filesystem_impl.h"

namespace Envoy {
namespace Api {

Event::DispatcherPtr Impl::allocateDispatcher(Event::TimeSystem& time_system) {
  return Event::DispatcherPtr{new Event::DispatcherImpl(time_system)};
}

Impl::Impl(Stats::Store& stats_store, std::chrono::milliseconds file_flush_interval_msec)
    : file_system_(file_flush_interval_msec, stats_store, *this) {}

Filesystem::FileSharedPtr Impl::createFile(const std::string& path, Event::Dispatcher& dispatcher,
                                           Thread::BasicLockable& lock) {
  return file_system_.createFile(path, dispatcher, lock);
}

bool Impl::fileExists(const std::string& path) { return Filesystem::fileExists(path); }

std::string Impl::fileReadToEnd(const std::string& path) { return Filesystem::fileReadToEnd(path); }

Thread::ThreadPtr Impl::createThread(std::function<void()> thread_routine) {
  return std::make_unique<Thread::ThreadImpl>(thread_routine);
}

} // namespace Api
} // namespace Envoy
