#pragma once

#include <chrono>
#include <string>

#include "envoy/api/api.h"
#include "envoy/event/timer.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/thread/thread.h"

#include "common/filesystem/filesystem_impl.h"

namespace Envoy {
namespace Api {

/**
 * Implementation of Api::Api
 */
class Impl : public Api::Api {
public:
  Impl(std::chrono::milliseconds file_flush_interval_msec, Thread::ThreadFactory& thread_factory,
       Stats::Store& stats_store);

  // Api::Api
  Event::DispatcherPtr allocateDispatcher(Event::TimeSystem& time_system) override;
  Filesystem::FileSharedPtr createFile(const std::string& path, Event::Dispatcher& dispatcher,
                                       Thread::BasicLockable& lock) override;
  bool fileExists(const std::string& path) override;
  std::string fileReadToEnd(const std::string& path) override;
  Thread::ThreadPtr createThread(std::function<void()> thread_routine) override;
  Filesystem::Instance& fileSystem() { return file_system_; }

private:
  Thread::ThreadFactory& thread_factory_;
  Filesystem::Instance file_system_;
};

} // namespace Api
} // namespace Envoy
