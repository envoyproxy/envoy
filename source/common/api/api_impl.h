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
  Impl(std::chrono::milliseconds file_flush_interval_msec, Thread::ThreadFactory& thread_factory,
       Stats::Store& stats_store);

  // Api::Api
  Event::DispatcherPtr allocateDispatcher(Event::TimeSystem& time_system) override;
  Thread::ThreadFactory& threadFactory() override;
  Filesystem::Instance& fileSystem() override;

private:
  Thread::ThreadFactory& thread_factory_;
  Filesystem::InstanceImpl file_system_;
};

} // namespace Api
} // namespace Envoy
