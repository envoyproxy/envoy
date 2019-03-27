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
  virtual ~Instance() {}

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
};

typedef std::shared_ptr<Instance> InstanceSharedPtr;

} // namespace ConnPool
} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
