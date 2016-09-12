#include "api_impl.h"

#include "common/event/dispatcher_impl.h"
#include "common/filesystem/filesystem_impl.h"

namespace Api {

Event::DispatcherPtr Impl::allocateDispatcher() {
  return Event::DispatcherPtr{new Event::DispatcherImpl()};
}

Impl::Impl(std::chrono::milliseconds flush_interval_msec)
    : os_sys_calls_(new Filesystem::OsSysCallsImpl()), flush_interval_msec_(flush_interval_msec) {}

Filesystem::FilePtr Impl::createFile(const std::string& path, Event::Dispatcher& dispatcher,
                                     Thread::BasicLockable& lock, Stats::Store& stats_store) {
  return Filesystem::FilePtr{new Filesystem::FileImpl(path, dispatcher, lock, *os_sys_calls_,
                                                      stats_store, flush_interval_msec_)};
}

bool Impl::fileExists(const std::string& path) { return Filesystem::fileExists(path); }

std::string Impl::fileReadToEnd(const std::string& path) { return Filesystem::fileReadToEnd(path); }

} // Api
