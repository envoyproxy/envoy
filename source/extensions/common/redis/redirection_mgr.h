#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Redis {

using RedirectCB = std::function<void()>;

/**
 * A manager for tracking redirection errors on a per cluster basis, and calling registered
 * callbacks when the error rate exceeds a configurable threshold (while ensuring that a minimum
 * time passes between calling the callback).
 */
class RedirectionManager {
public:
  class Handle {
  public:
    virtual ~Handle() = default;
  };

  using HandlePtr = std::unique_ptr<Handle>;

  virtual ~RedirectionManager() = default;

  /**
   * Notifies the manager that a redirection error has been received for a given cluster.
   * @param cluster_name is the name of the cluster.
   * @return bool true if a cluster's registered callback with is scheduled
   * to be called from the main thread dispatcher, false otherwise.
   */
  virtual bool onRedirection(const std::string& cluster_name) PURE;

  /**
   * Register a cluster to be tracked by the manager (called by main thread only).
   * @param cluster_name is the name of the cluster.
   * @param min_time_between_triggering is the minimum amount of time that must pass between
   * callback invocations (redirects ignored and not counted during this time).
   * @param redirects_threshold is the number of redirects that must be reached to consider
   * calling the callback.
   * @param cb is the cluster callback function.
   * @return HandlePtr is a smart pointer to an opaque Handle that will unregister the cluster upon
   * destruction.
   */
  virtual HandlePtr registerCluster(const std::string& cluster_name,
                                    std::chrono::milliseconds min_time_between_triggering,
                                    uint32_t redirects_threshold, const RedirectCB& cb) PURE;
};

using RedirectionManagerSharedPtr = std::shared_ptr<RedirectionManager>;

} // namespace Redis
} // namespace Common
} // namespace Extensions
} // namespace Envoy
