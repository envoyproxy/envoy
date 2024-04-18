#pragma once

#include "envoy/common/pure.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/thread/thread.h"

namespace Envoy {
namespace Server {

/**
 * Interface for providing platform specific implementation of OS facilities.
 */
class Platform {
public:
  virtual ~Platform() = default;

  /**
   * @return platform specific thread factory.
   */
  virtual Thread::ThreadFactory& threadFactory() PURE;

  /**
   * @return platform specific filesystem facility.
   */
  virtual Filesystem::Instance& fileSystem() PURE;

  /**
   * @return platform specific core dump facility.
   */
  virtual bool enableCoreDump() PURE;
};

} // namespace Server
} // namespace Envoy
