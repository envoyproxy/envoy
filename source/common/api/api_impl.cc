#include "api_impl.h"

#include "common/event/dispatcher_impl.h"
#include "common/filesystem/filesystem_impl.h"

namespace Api {

Event::DispatcherPtr Impl::allocateDispatcher() {
  return Event::DispatcherPtr{new Event::DispatcherImpl()};
}

Impl::Impl(std::chrono::milliseconds file_flush_interval_msec)
    : os_sys_calls_(new Filesystem::OsSysCallsImpl()),
      file_flush_interval_msec_(file_flush_interval_msec) {}

Filesystem::FileSharedPtr Impl::createFile(const std::string& path, Event::Dispatcher& dispatcher,
                                           Thread::BasicLockable& lock, Stats::Store& stats_store) {
  return Filesystem::FileSharedPtr{new Filesystem::FileImpl(
      path, dispatcher, lock, *os_sys_calls_, stats_store, file_flush_interval_msec_)};
}

bool Impl::fileExists(const std::string& path) { return Filesystem::fileExists(path); }

std::string Impl::fileReadToEnd(const std::string& path) { return Filesystem::fileReadToEnd(path); }

} // Api
