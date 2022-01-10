#pragma once

#include <memory>
#include <string>

#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/scaled_range_timer_manager.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/server/process_context.h"
#include "envoy/stats/custom_stat_namespaces.h"
#include "envoy/stats/store.h"
#include "envoy/thread/thread.h"

namespace Envoy {
namespace Api {

/**
 * "Public" API that different components use to interact with the various system abstractions.
 */
class Api {
public:
  virtual ~Api() = default;

  /**
   * Allocate a dispatcher.
   * @param name the identity name for a dispatcher, e.g. "worker_2" or "main_thread".
   *             This name will appear in per-handler/worker statistics, such as
   *             "server.worker_2.watchdog_miss".
   * @return Event::DispatcherPtr which is owned by the caller.
   */
  virtual Event::DispatcherPtr allocateDispatcher(const std::string& name) PURE;

  /**
   * Allocate a dispatcher.
   * @param name the identity name for a dispatcher, e.g. "worker_2" or "main_thread".
   *             This name will appear in per-handler/worker statistics, such as
   *             "server.worker_2.watchdog_miss".
   * @param scaled_timer_factory the factory to use when creating the scaled timer manager.
   * @return Event::DispatcherPtr which is owned by the caller.
   */
  virtual Event::DispatcherPtr
  allocateDispatcher(const std::string& name,
                     const Event::ScaledRangeTimerManagerFactory& scaled_timer_factory) PURE;

  /**
   * Allocate a dispatcher.
   * @param name the identity name for a dispatcher, e.g. "worker_2" or "main_thread".
   *             This name will appear in per-handler/worker statistics, such as
   *             "server.worker_2.watchdog_miss".
   * @param watermark_factory the watermark factory, ownership is transferred to the dispatcher.
   * @return Event::DispatcherPtr which is owned by the caller.
   */
  virtual Event::DispatcherPtr
  allocateDispatcher(const std::string& name, Buffer::WatermarkFactoryPtr&& watermark_factory) PURE;

  /**
   * @return a reference to the ThreadFactory
   */
  virtual Thread::ThreadFactory& threadFactory() PURE;

  /**
   * @return a reference to the Filesystem::Instance
   */
  virtual Filesystem::Instance& fileSystem() PURE;

  /**
   * @return a reference to the TimeSource
   */
  virtual TimeSource& timeSource() PURE;

  /**
   * @return a reference to the root Stats::Scope
   */
  virtual Stats::Scope& rootScope() PURE;

  /**
   * @return a reference to the RandomGenerator.
   */
  virtual Random::RandomGenerator& randomGenerator() PURE;

  /**
   * @return an optional reference to the ProcessContext
   */
  virtual ProcessContextOptRef processContext() PURE;

  /**
   * @return the bootstrap Envoy started with.
   */
  virtual const envoy::config::bootstrap::v3::Bootstrap& bootstrap() const PURE;

  /**
   * @return a reference to the Stats::CustomStatNamespaces.
   */
  virtual Stats::CustomStatNamespaces& customStatNamespaces() PURE;
};

using ApiPtr = std::unique_ptr<Api>;

} // namespace Api
} // namespace Envoy
