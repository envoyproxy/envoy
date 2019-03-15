#pragma once

#include <chrono>
#include <string>

#include "envoy/api/api.h"
#include "envoy/event/timer.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/stats/store.h"
#include "envoy/thread/thread.h"

#include "common/filesystem/filesystem_impl.h"

namespace Envoy {
namespace Api {

/**
 * Implementation of Api::Api
 */
class Impl : public Api {
public:
  Impl(Thread::ThreadFactory& thread_factory, Stats::Store& stats_store,
       Event::TimeSystem& time_system);

  // Api::Api
  Event::DispatcherPtr allocateDispatcher() override;
  Event::DispatcherPtr allocateDispatcher(Buffer::WatermarkFactoryPtr&& watermark_factory) override;
  Thread::ThreadFactory& threadFactory() override { return thread_factory_; }
  Filesystem::Instance& fileSystem() override { return file_system_; }
  TimeSource& timeSource() override { return time_system_; }
  Stats::Store& statsStore() override { return stats_store_; }

private:
  Thread::ThreadFactory& thread_factory_;
  Filesystem::InstanceImpl file_system_;
  Event::TimeSystem& time_system_;
  Stats::Store& stats_store_;
};

} // namespace Api
} // namespace Envoy
