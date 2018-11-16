#include "common/http/wrapped_connection_pool.h"

namespace Envoy {
namespace Http {

WrappedConnectionPool::WrappedConnectionPool(
  std::function<Http::ConnectionPool::InstancePtr()> builder):
  builder_(builder)
{

}

Http::Protocol WrappedConnectionPool::protocol() const {
  // TODO(klarose): Get from sub-pool somehow
  return Http::Protocol::Http11;
}
void WrappedConnectionPool::addDrainedCallback(DrainedCb /*unused*/) {
}
void WrappedConnectionPool::drainConnections() {

}

ConnectionPool::Cancellable*
 WrappedConnectionPool::newStream(Http::StreamDecoder& /* unused */,
                                  ConnectionPool::Callbacks& /*unused */) {
  return nullptr;
}

} // namespace Http
} // namespace Envoy
