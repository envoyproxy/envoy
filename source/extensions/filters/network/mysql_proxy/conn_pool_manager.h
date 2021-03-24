#pragma once

#include "extensions/filters/network/mysql_proxy/new_conn_pool_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {
namespace ConnPool {

/*
 * cluster connection manager, maintain connection pool per host.
 */
class ConnectionManager {
public:
  virtual ~ConnectionManager() = default;
  // now use defualt lb to choose host.
  virtual ConnectionPool::Cancellable*
  newConnection(Envoy::Tcp::ConnectionPool::Callbacks& callbacks) PURE;
};

} // namespace ConnPool
} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy