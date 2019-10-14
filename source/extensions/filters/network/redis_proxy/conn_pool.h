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
  virtual Common::Redis::Client::PoolRequest* makeRequest(const std::string& hash_key,
                                                          Common::Redis::RespValueSharedPtr request,
                                                          PoolCallbacks& callbacks) PURE;

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
  makeRequestToHost(const std::string& host_address, Common::Redis::RespValueSharedPtr request,
                    PoolCallbacks& callbacks) PURE;
};

using InstanceSharedPtr = std::shared_ptr<Instance>;

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
