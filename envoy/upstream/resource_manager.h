#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/resource.h"

namespace Envoy {
namespace Upstream {

/**
 * Resource priority classes. The parallel NumResourcePriorities constant allows defining fixed
 * arrays for each priority, but does not pollute the enum.
 */
enum class ResourcePriority { Default, High };
const size_t NumResourcePriorities = 2;

/**
 * RAII wrapper that increments a resource on construction and decrements it on destruction.
 */
class ResourceAutoIncDec {
public:
  ResourceAutoIncDec(ResourceLimit& resource) : resource_(resource) { resource_.inc(); }
  ~ResourceAutoIncDec() { resource_.dec(); }

private:
  ResourceLimit& resource_;
};

using ResourceAutoIncDecPtr = std::unique_ptr<ResourceAutoIncDec>;

/**
 * Global resource manager that loosely synchronizes maximum connections, pending requests, etc.
 * NOTE: Currently this is used on a per cluster basis. In the future we may consider also chaining
 *       this with a global resource manager.
 */
class ResourceManager {
public:
  virtual ~ResourceManager() = default;

  /**
   * @return ResourceLimit& active TCP connections and UDP sessions.
   */
  virtual ResourceLimit& connections() PURE;

  /**
   * @return ResourceLimit& active pending requests (requests that have not yet been attached to a
   *         connection pool connection).
   */
  virtual ResourceLimit& pendingRequests() PURE;

  /**
   * @return ResourceLimit& active requests (requests that are currently bound to a connection pool
   *         connection and are awaiting response).
   */
  virtual ResourceLimit& requests() PURE;

  /**
   * @return ResourceLimit& active retries.
   */
  virtual ResourceLimit& retries() PURE;

  /**
   * @return ResourceLimit& active connection pools.
   */
  virtual ResourceLimit& connectionPools() PURE;
};

} // namespace Upstream
} // namespace Envoy
