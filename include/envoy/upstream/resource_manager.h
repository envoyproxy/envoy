#pragma once

#include <cstddef>
#include <cstdint>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Upstream {

/**
 * Resource priority classes. The parallel NumResourcePriorities constant allows defining fixed
 * arrays for each priority, but does not pollute the enum.
 */
enum class ResourcePriority { Default, High };
const size_t NumResourcePriorities = 2;

/**
 * An individual resource tracked by the resource manager.
 */
class Resource {
public:
  virtual ~Resource() {}

  /**
   * @return true if the resource can be created.
   */
  virtual bool canCreate() PURE;

  /**
   * Increment the resource count.
   */
  virtual void inc() PURE;

  /**
   * Decrement the resource count.
   */
  virtual void dec() PURE;

  /**
   * @return the current maximum allowed number of this resource.
   */
  virtual uint64_t max() PURE;
};

/**
 * Global resource manager that loosely synchronizes maximum connections, pending requests, etc.
 * NOTE: Currently this is used on a per cluster basis. In the future we may consider also chaining
 *       this with a global resource manager.
 */
class ResourceManager {
public:
  virtual ~ResourceManager() {}

  /**
   * @return Resource& active TCP connections.
   */
  virtual Resource& connections() PURE;

  /**
   * @return Resource& active pending requests (requests that have not yet been attached to a
   *         connection pool connection).
   */
  virtual Resource& pendingRequests() PURE;

  /**
   * @return Resource& active requests (requests that are currently bound to a connection pool
   *         connection and are awaiting response).
   */
  virtual Resource& requests() PURE;

  /**
   * @return Resource& active retries.
   */
  virtual Resource& retries() PURE;
};

} // namespace Upstream
} // namespace Envoy
