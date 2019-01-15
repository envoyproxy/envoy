#pragma once

#include <chrono>
#include <string>

#include "envoy/api/api.h"
#include "envoy/event/timer.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/thread/thread.h"

#include "common/filesystem/stats_instance_impl.h"

namespace Envoy {
namespace Api {

/**
 * Implementation of Api::Api
 */
class Impl : public Api::Api {
public:
  Impl(std::chrono::milliseconds file_flush_interval_msec, Thread::ThreadFactory& thread_factory,
       Stats::Store& stats_store, Filesystem::Instance& file_system);

  // Api::Api
  Event::DispatcherPtr allocateDispatcher(Event::TimeSystem& time_system) override;
  Thread::ThreadFactory& threadFactory() override;
  Filesystem::StatsInstance& fileSystem() override;

private:
  Thread::ThreadFactory& thread_factory_;
  Filesystem::StatsInstanceImpl file_system_;
};

} // namespace Api
} // namespace Envoy
