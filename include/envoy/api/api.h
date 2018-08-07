#pragma once

#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
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
   * Create/open a local file that supports async appending.
   * @param path supplies the file path.
   * @param dispatcher supplies the dispatcher uses for async flushing.
   * @param lock supplies the lock to use for cross thread appends.
   */
  virtual Filesystem::FileSharedPtr createFile(const std::string& path,
                                               Event::Dispatcher& dispatcher,
                                               Thread::BasicLockable& lock,
                                               Stats::Store& stats_store) PURE;

  /**
   * @return bool whether a file exists and can be opened for read on disk.
   */
  virtual bool fileExists(const std::string& path) PURE;

  /**
   * @return file content.
   */
  virtual std::string fileReadToEnd(const std::string& path) PURE;
};

typedef std::unique_ptr<Api> ApiPtr;

} // namespace Api
} // namespace Envoy
