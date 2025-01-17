#pragma once

#include <chrono>
#include <string>

#include "envoy/api/api.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/timer.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/network/socket.h"
#include "envoy/thread/thread.h"

#include "source/common/stats/custom_stat_namespaces_impl.h"

namespace Envoy {
namespace Api {

/**
 * Implementation of Api::Api
 */
class Impl : public Api {
public:
  Impl(Thread::ThreadFactory& thread_factory, Stats::Store& store, Event::TimeSystem& time_system,
       Filesystem::Instance& file_system, Random::RandomGenerator& random_generator,
       const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
       const ProcessContextOptRef& process_context = absl::nullopt,
       Buffer::WatermarkFactorySharedPtr watermark_factory = nullptr);

  // Api::Api
  Event::DispatcherPtr allocateDispatcher(const std::string& name) override;
  Event::DispatcherPtr
  allocateDispatcher(const std::string& name,
                     const Event::ScaledRangeTimerManagerFactory& scaled_timer_factory) override;
  Event::DispatcherPtr allocateDispatcher(const std::string& name,
                                          Buffer::WatermarkFactoryPtr&& watermark_factory) override;
  Thread::ThreadFactory& threadFactory() override { return thread_factory_; }
  Filesystem::Instance& fileSystem() override { return file_system_; }
  TimeSource& timeSource() override { return time_system_; }
  Stats::Scope& rootScope() override { return *store_.rootScope(); }
  Random::RandomGenerator& randomGenerator() override { return random_generator_; }
  Stats::CustomStatNamespaces& customStatNamespaces() override { return custom_stat_namespaces_; }
  const envoy::config::bootstrap::v3::Bootstrap& bootstrap() const override { return bootstrap_; }
  ProcessContextOptRef processContext() override { return process_context_; }

private:
  Thread::ThreadFactory& thread_factory_;
  Stats::Store& store_;
  Event::TimeSystem& time_system_;
  Filesystem::Instance& file_system_;
  Random::RandomGenerator& random_generator_;
  const envoy::config::bootstrap::v3::Bootstrap& bootstrap_;
  Stats::CustomStatNamespacesImpl custom_stat_namespaces_;
  ProcessContextOptRef process_context_;
  const Buffer::WatermarkFactorySharedPtr watermark_factory_;
};

} // namespace Api
} // namespace Envoy
