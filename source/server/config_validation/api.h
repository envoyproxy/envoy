#pragma once

#include "envoy/api/api.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/timer.h"
#include "envoy/filesystem/filesystem.h"

#include "source/common/api/api_impl.h"

namespace Envoy {
namespace Api {

/**
 * Config-validation-only implementation of Api::Api. Delegates to Api::Impl,
 * except for allocateDispatcher() which sets up a ValidationDispatcher.
 */
class ValidationImpl : public Impl {
public:
  ValidationImpl(Thread::ThreadFactory& thread_factory, Stats::Store& stats_store,
                 Event::TimeSystem& time_system, Filesystem::Instance& file_system,
                 Random::RandomGenerator& random_generator,
                 const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                 const ProcessContextOptRef& process_context = absl::nullopt);

  Event::DispatcherPtr allocateDispatcher(const std::string& name) override;
  Event::DispatcherPtr allocateDispatcher(const std::string& name,
                                          const Event::ScaledRangeTimerManagerFactory&) override;
  Event::DispatcherPtr allocateDispatcher(const std::string& name,
                                          Buffer::WatermarkFactoryPtr&& watermark_factory) override;

private:
  Event::TimeSystem& time_system_;
};

} // namespace Api
} // namespace Envoy
