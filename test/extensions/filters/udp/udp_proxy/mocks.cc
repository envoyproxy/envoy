#include "test/extensions/filters/udp/udp_proxy/mocks.h"

#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {

MockReadFilterCallbacks::MockReadFilterCallbacks() {
  ON_CALL(*this, sessionId()).WillByDefault(Return(session_id_));
  ON_CALL(*this, streamInfo()).WillByDefault(ReturnRef(stream_info_));
  ON_CALL(*this, continueFilterChain()).WillByDefault(Return(true));
}
MockReadFilterCallbacks::~MockReadFilterCallbacks() = default;

MockWriteFilterCallbacks::MockWriteFilterCallbacks() {
  ON_CALL(*this, sessionId()).WillByDefault(Return(session_id_));
  ON_CALL(*this, streamInfo()).WillByDefault(ReturnRef(stream_info_));
}
MockWriteFilterCallbacks::~MockWriteFilterCallbacks() = default;

MockUdpTunnelingConfig::MockUdpTunnelingConfig(Http::HeaderEvaluator& header_evaluator)
    : header_evaluator_(header_evaluator) {
  ON_CALL(*this, proxyHost(_)).WillByDefault(Return(default_proxy_host_));
  ON_CALL(*this, targetHost(_)).WillByDefault(Return(default_target_host_));
  ON_CALL(*this, proxyPort()).WillByDefault(ReturnRef(default_proxy_port_));
  ON_CALL(*this, defaultTargetPort()).WillByDefault(Return(default_target_port_));
  ON_CALL(*this, postPath()).WillByDefault(ReturnRef(post_path_));
  ON_CALL(*this, headerEvaluator()).WillByDefault(ReturnRef(header_evaluator_));
}
MockUdpTunnelingConfig::~MockUdpTunnelingConfig() = default;

MockTunnelCreationCallbacks::~MockTunnelCreationCallbacks() = default;
MockUpstreamTunnelCallbacks::~MockUpstreamTunnelCallbacks() = default;
MockHttpStreamCallbacks::~MockHttpStreamCallbacks() = default;

} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
