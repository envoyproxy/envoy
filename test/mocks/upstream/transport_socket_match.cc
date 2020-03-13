#include "test/mocks/upstream/transport_socket_match.h"

#include "common/network/raw_buffer_socket.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Upstream {

MockTransportSocketMatcher::MockTransportSocketMatcher()
    : MockTransportSocketMatcher(std::make_unique<Network::RawBufferSocketFactory>()) {}

MockTransportSocketMatcher::MockTransportSocketMatcher(Network::TransportSocketFactoryPtr factory)
    : socket_factory_(std::move(factory)),
      stats_({ALL_TRANSPORT_SOCKET_MATCH_STATS(POOL_COUNTER_PREFIX(stats_store_, "test"))}) {
  ON_CALL(*this, resolve(_))
      .WillByDefault(Return(TransportSocketMatcher::MatchData(*socket_factory_, stats_, "test")));
}

MockTransportSocketMatcher::~MockTransportSocketMatcher() = default;
} // namespace Upstream
} // namespace Envoy
