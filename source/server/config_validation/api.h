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
  ValidationImpl(std::chrono::milliseconds file_flush_interval_msec,
                 Thread::ThreadFactory& thread_factory, Stats::Store& stats_store);

  Event::DispatcherPtr allocateDispatcher(Event::TimeSystem&) override;
};

} // namespace Api
} // namespace Envoy
