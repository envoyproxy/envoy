#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.h"

#include "extensions/filters/network/redis_proxy/conn_pool.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

/*
 * Decorator of a connection pool in order to enable key based routing.
 */
class Router {
public:
  virtual ~Router() = default;

  /**
   * Forwards the request to the connection pool that matches a route or uses the wildcard route
   * when no match is found.
   * @param key supplies the key of the current command.
   * @param request supplies the RESP request to make.
   * @param callbacks supplies the request callbacks.
   * @return PoolRequest* a handle to the active request or nullptr if the request could not be made
   *         for some reason.
   */
  virtual Common::Redis::Client::PoolRequest*
  makeRequest(const std::string& key, const Common::Redis::RespValue& request,
              Common::Redis::Client::PoolCallbacks& callbacks) PURE;
};

typedef std::unique_ptr<Router> RouterPtr;

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
