#pragma once

#include <memory>
#include <string>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/stats/store.h"
#include "envoy/thread/thread.h"

namespace Envoy {
namespace Api {

/**
 * "Public" API that different components use to interact with the various system abstractions.
 */
class Api {
public:
  virtual ~Api() {}

  /**
   * Allocate a dispatcher.
   * @param time_source the time source.
   * @return Event::DispatcherPtr which is owned by the caller.
   */
  virtual Event::DispatcherPtr allocateDispatcher(Event::TimeSystem& time_system) PURE;

  /**
   * @return a reference to the ThreadFactory
   */
  virtual Thread::ThreadFactory& threadFactory() PURE;

  /**
   * @return a reference to the Filesystem::Instance
   */
  virtual Filesystem::Instance& fileSystem() PURE;
};

typedef std::unique_ptr<Api> ApiPtr;

} // namespace Api
} // namespace Envoy