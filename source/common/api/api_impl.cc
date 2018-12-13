#include "common/api/api_impl.h"

#include <chrono>
#include <string>

#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"
#include "common/filesystem/filesystem_impl.h"

namespace Envoy {
namespace Api {

Impl::Impl(std::chrono::milliseconds file_flush_interval_msec,
           Thread::ThreadFactory& thread_factory, Stats::Store& stats_store)
    : thread_factory_(thread_factory),
      file_system_(file_flush_interval_msec, thread_factory, stats_store) {}

Event::DispatcherPtr Impl::allocateDispatcher(Event::TimeSystem& time_system) {
  return Event::DispatcherPtr{new Event::DispatcherImpl(time_system)};
}

Filesystem::FileSharedPtr Impl::createFile(const std::string& path, Event::Dispatcher& dispatcher,
                                           Thread::BasicLockable& lock) {
  return file_system_.createFile(path, dispatcher, lock);
}

bool Impl::fileExists(const std::string& path) { return Filesystem::fileExists(path); }

std::string Impl::fileReadToEnd(const std::string& path) { return Filesystem::fileReadToEnd(path); }

Thread::ThreadPtr Impl::createThread(std::function<void()> thread_routine) {
  return thread_factory_.createThread(thread_routine);
}

} // namespace Api
} // namespace Envoy
