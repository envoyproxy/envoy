#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

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
  virtual ~Resource() = default;

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
   * Decrement the resource count by a specific amount.
   */
  virtual void decBy(uint64_t amount) PURE;

  /**
   * @return the current maximum allowed number of this resource.
   */
  virtual uint64_t max() PURE;

  /**
   * @return the current resource count.
   */
  virtual uint64_t count() const PURE;
};

/**
 * RAII wrapper that increments a resource on construction and decrements it on destruction.
 */
class ResourceAutoIncDec {
public:
  ResourceAutoIncDec(Resource& resource) : resource_(resource) { resource_.inc(); }
  ~ResourceAutoIncDec() { resource_.dec(); }

private:
  Resource& resource_;
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
   * @return Resource& active TCP connections and UDP sessions.
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

  /**
   * @return Resource& active connection pools.
   */
  virtual Resource& connectionPools() PURE;
};

} // namespace Upstream
} // namespace Envoy
