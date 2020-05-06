#pragma once

#include <memory>
#include <string>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/server/process_context.h"
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
   * @return a constant reference to the root Stats::Scope
   */
  virtual const Stats::Scope& rootScope() PURE;

  /**
   * @return a reference to the stats symbol table.
   */
  virtual const Stats::SymbolTable& symbolTable() PURE;

  /**
   * @return an optional reference to the ProcessContext
   */
  virtual ProcessContextOptRef processContext() PURE;
};

using ApiPtr = std::unique_ptr<Api>;

} // namespace Api
} // namespace Envoy
