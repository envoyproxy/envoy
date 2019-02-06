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
   * @return Event::DispatcherPtr which is owned by the caller.
   */
  virtual Event::DispatcherPtr allocateDispatcher() PURE;

  /**
   * @return a reference to the ThreadFactory
   */
  virtual Thread::ThreadFactory& threadFactory() PURE;

  /**
   * @return a reference to the Filesystem::Instance
   */
  virtual Filesystem::Instance& fileSystem() PURE;

  /**
   * @return a reference to the TimeSystem
   * TODO(jmarantz): change this to return a TimeSource.
   */
  virtual Event::TimeSystem& timeSystem() PURE;
};

typedef std::unique_ptr<Api> ApiPtr;

} // namespace Api
} // namespace Envoy
