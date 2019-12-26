#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "envoy/upstream/cluster_manager.h"

#include "extensions/filters/network/common/redis/client.h"
#include "extensions/filters/network/common/redis/codec.h"

#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace ConnPool {

/**
 * Outbound request callbacks.
 */
class PoolCallbacks {
public:
  virtual ~PoolCallbacks() = default;

  /**
   * Called when a pipelined response is received.
   * @param value supplies the response which is now owned by the callee.
   */
  virtual void onResponse(Common::Redis::RespValuePtr&& value) PURE;

  /**
   * Called when a network/protocol error occurs and there is no response.
   */
  virtual void onFailure() PURE;
};

/**
 * A variant that either holds a shared pointer to a single server request or a composite array
 * resp value. This is for performance reason to avoid creating RespValueSharedPtr for each
 * composite arrays.
 */
using RespVariant =
    absl::variant<const Common::Redis::RespValue, Common::Redis::RespValueConstSharedPtr>;

/**
 * A redis connection pool. Wraps M connections to N upstream hosts, consistent hashing,
 * pipelining, failure handling, etc.
 */
class Instance {
public:
  virtual ~Instance() = default;

  /**
   * Makes a redis request.
   * @param hash_key supplies the key to use for consistent hashing.
   * @param request supplies the request to make.
   * @param callbacks supplies the request completion callbacks.
   * @return PoolRequest* a handle to the active request or nullptr if the request could not be made
   *         for some reason.
   */
  virtual Common::Redis::Client::PoolRequest*
  makeRequest(const std::string& hash_key, RespVariant&& request, PoolCallbacks& callbacks) PURE;

  /**
   * Notify the redirection manager singleton that a redirection error has been received from an
   * upstream server associated with the pool's associated cluster.
   * @return bool true if a cluster's registered callback with the redirection manager is scheduled
   * to be called from the main thread dispatcher, false otherwise.
   */
  virtual bool onRedirection() PURE;
};

using InstanceSharedPtr = std::shared_ptr<Instance>;

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
