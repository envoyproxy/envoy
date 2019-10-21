#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "envoy/upstream/cluster_manager.h"

#include "extensions/filters/network/common/redis/client.h"
#include "extensions/filters/network/common/redis/codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {
namespace ConnPool {

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
  makeRequest(const std::string& hash_key, const Common::Redis::RespValue& request,
              Common::Redis::Client::PoolCallbacks& callbacks) PURE;

  /**
   * Makes a redis request based on IP address and TCP port of the upstream host (e.g., moved/ask
   * cluster redirection).
   * @param host_address supplies the IP address and TCP port of the upstream host to receive the
   * request.
   * @param request supplies the Redis request to make.
   * @param callbacks supplies the request completion callbacks.
   * @return PoolRequest* a handle to the active request or nullptr if the request could not be made
   *         for some reason.
   */
  virtual Common::Redis::Client::PoolRequest*
  makeRequestToHost(const std::string& host_address, const Common::Redis::RespValue& request,
                    Common::Redis::Client::PoolCallbacks& callbacks) PURE;

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
