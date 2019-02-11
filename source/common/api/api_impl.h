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
class Impl : public Api {
public:
  Impl(Thread::ThreadFactory& thread_factory, Event::TimeSystem& time_system);

  // Api::Api
  Event::DispatcherPtr allocateDispatcher() override;
  Thread::ThreadFactory& threadFactory() override { return thread_factory_; }
  Filesystem::Instance& fileSystem() override { return file_system_; }
  TimeSource& timeSource() override { return time_system_; }
  Event::TimeSystem& timeSystemForDispatcher() override { return time_system_; }

private:
  Thread::ThreadFactory& thread_factory_;
  Filesystem::InstanceImpl file_system_;
  Event::TimeSystem& time_system_;
};

} // namespace Api
} // namespace Envoy
