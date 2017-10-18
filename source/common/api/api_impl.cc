#include "common/api/api_impl.h"

#include <chrono>
#include <string>

#include "common/event/dispatcher_impl.h"
#include "common/filesystem/filesystem_impl.h"

namespace Envoy {
namespace Api {

Event::DispatcherPtr Impl::allocateDispatcher() {
  return Event::DispatcherPtr{new Event::DispatcherImpl()};
}

Impl::Impl(std::chrono::milliseconds file_flush_interval_msec)
    : file_flush_interval_msec_(file_flush_interval_msec) {}

Filesystem::FileSharedPtr Impl::createFile(const std::string& path, Event::Dispatcher& dispatcher,
                                           Thread::BasicLockable& lock, Stats::Store& stats_store) {
  return std::make_shared<Filesystem::FileImpl>(path, dispatcher, lock, stats_store,
                                                file_flush_interval_msec_);
}

bool Impl::fileExists(const std::string& path) { return Filesystem::fileExists(path); }

std::string Impl::fileReadToEnd(const std::string& path) { return Filesystem::fileReadToEnd(path); }

} // namespace Api
} // namespace Envoy
