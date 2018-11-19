#pragma once

#include <functional>

#include "envoy/http/conn_pool.h"

#include "common/http/connection_mapper.h"
#include "common/singleton/threadsafe_singleton.h"

namespace Envoy {
namespace Http {

/**
 * A factory for connection pool mappers.
 * This factory will build various forms of connection pool mappers according to the provided
 * parameters. This allows the WrappedConnectionPool to not care about how the details of which
 * mapper it is actually using.
 *
 * The factory is a singleton. It may be accessed using the ThreadSafeSingleton api.
 */
class ConnectionMapperFactory {
public:
  using ConnPoolBuilder = std::function<ConnectionPool::InstancePtr()>;

  virtual ~ConnectionMapperFactory();

  virtual ConnectionMapperPtr createSrcTransparentMapper(const ConnPoolBuilder& builder);
};

using ConnectionMapperFactorySingleton = ThreadSafeSingleton<ConnectionMapperFactory>;

} // namespace Http
} // namespace Envoy
