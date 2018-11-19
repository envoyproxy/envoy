#include "common/http/wrapped_connection_pool.h"

namespace Envoy {
namespace Http {

WrappedConnectionPool::WrappedConnectionPool(std::unique_ptr<ConnectionMapper> mapper,
                                             Protocol protocol)
    : mapper_(std::move(mapper)), protocol_(protocol) {}

Http::Protocol WrappedConnectionPool::protocol() const { return protocol_; }
void WrappedConnectionPool::addDrainedCallback(DrainedCb /*unused*/) {}
void WrappedConnectionPool::drainConnections() {}

ConnectionPool::Cancellable*
WrappedConnectionPool::newStream(Http::StreamDecoder& /* unused */,
                                 ConnectionPool::Callbacks& /*unused */) {
  return nullptr;
}

ConnectionPool::Cancellable*
WrappedConnectionPool::newStream(Http::StreamDecoder& /* unused */,
                                 ConnectionPool::Callbacks& /*unused */,
                                 const Upstream::LoadBalancerContext& /*unused */) {
  return nullptr;
}
} // namespace Http
} // namespace Envoy
