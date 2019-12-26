#pragma once

#include "envoy/api/api.h"
#include "envoy/event/timer.h"
#include "envoy/filesystem/filesystem.h"

#include "common/api/api_impl.h"

namespace Envoy {
namespace Api {

/**
 * Config-validation-only implementation of Api::Api. Delegates to Api::Impl,
 * except for allocateDispatcher() which sets up a ValidationDispatcher.
 */
class ValidationImpl : public Impl {
public:
  ValidationImpl(Thread::ThreadFactory& thread_factory, Stats::Store& stats_store,
                 Event::TimeSystem& time_system, Filesystem::Instance& file_system);

  Event::DispatcherPtr allocateDispatcher() override;
  Event::DispatcherPtr allocateDispatcher(Buffer::WatermarkFactoryPtr&& watermark_factory) override;

private:
  Event::TimeSystem& time_system_;
};

} // namespace Api
} // namespace Envoy
