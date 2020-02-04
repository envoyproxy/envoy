#pragma once

#include <chrono>
#include <string>

#include "envoy/api/api.h"
#include "envoy/event/timer.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/thread/thread.h"

namespace Envoy {
namespace Api {

/**
 * Implementation of Api::Api
 */
class Impl : public Api {
public:
  Impl(Thread::ThreadFactory& thread_factory, Stats::Store& store, Event::TimeSystem& time_system,
       Filesystem::Instance& file_system,
       const ProcessContextOptRef& process_context = absl::nullopt);

  // Api::Api
  Event::DispatcherPtr allocateDispatcher() override;
  Event::DispatcherPtr allocateDispatcher(Buffer::WatermarkFactoryPtr&& watermark_factory) override;
  Thread::ThreadFactory& threadFactory() override { return thread_factory_; }
  Filesystem::Instance& fileSystem() override { return file_system_; }
  TimeSource& timeSource() override { return time_system_; }
  const Stats::Scope& rootScope() override { return store_; }
  ProcessContextOptRef processContext() override { return process_context_; }

private:
  Thread::ThreadFactory& thread_factory_;
  Stats::Store& store_;
  Event::TimeSystem& time_system_;
  Filesystem::Instance& file_system_;
  ProcessContextOptRef process_context_;
};

} // namespace Api
} // namespace Envoy
