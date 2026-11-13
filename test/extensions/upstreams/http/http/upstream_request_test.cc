#include "source/common/network/address_impl.h"
#include "source/extensions/upstreams/http/http/upstream_request.h"

#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Http {
namespace {

class HttpUpstreamTest : public testing::Test {
public:
  HttpUpstreamTest() {
    ON_CALL(connection_info_, downstreamTransportFailureReason())
        .WillByDefault(Return("connection failure"));
    // By default, the connection is to an internal listener.
    setConnectionRemoteAddress(
        std::make_shared<Network::Address::EnvoyInternalInstance>("internal_listener"));
  }

  void setConnectionRemoteAddress(Network::Address::InstanceConstSharedPtr address) {
    connection_info_.downstream_connection_info_provider_->setRemoteAddress(std::move(address));
  }

  NiceMock<Router::MockUpstreamToDownstream> upstream_to_downstream_;
  NiceMock<Envoy::Http::MockRequestEncoder> encoder_;
  NiceMock<StreamInfo::MockStreamInfo> connection_info_;
};

// A reset caused by the connection reports the connection's transport failure reason if the codec
// supplies no details.
TEST_F(HttpUpstreamTest, ConnectionResetReportsConnectionFailureReason) {
  HttpUpstream upstream(upstream_to_downstream_, &encoder_, &connection_info_);
  for (const auto reason : {Envoy::Http::StreamResetReason::ConnectionTermination,
                            Envoy::Http::StreamResetReason::LocalConnectionFailure,
                            Envoy::Http::StreamResetReason::RemoteConnectionFailure}) {
    EXPECT_CALL(upstream_to_downstream_, onResetStream(reason, "connection failure"));
    upstream.onResetStream(reason, "");
  }
}

// Details supplied by the codec take precedence.
TEST_F(HttpUpstreamTest, ConnectionResetKeepsCodecDetails) {
  HttpUpstream upstream(upstream_to_downstream_, &encoder_, &connection_info_);
  EXPECT_CALL(upstream_to_downstream_,
              onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, "details"));
  upstream.onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, "details");
}

// The connection's transport failure reason is only reported for a connection to an internal
// listener.
TEST_F(HttpUpstreamTest, ConnectionResetIgnoresFailureReasonOfNonInternalConnection) {
  setConnectionRemoteAddress(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 80));
  HttpUpstream upstream(upstream_to_downstream_, &encoder_, &connection_info_);
  EXPECT_CALL(upstream_to_downstream_,
              onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, ""));
  upstream.onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, "");
}

// Resets that are not caused by the connection are reported as is.
TEST_F(HttpUpstreamTest, StreamResetIgnoresConnectionFailureReason) {
  HttpUpstream upstream(upstream_to_downstream_, &encoder_, &connection_info_);
  EXPECT_CALL(upstream_to_downstream_,
              onResetStream(Envoy::Http::StreamResetReason::RemoteReset, ""));
  upstream.onResetStream(Envoy::Http::StreamResetReason::RemoteReset, "");
}

TEST_F(HttpUpstreamTest, NoConnectionInfo) {
  HttpUpstream upstream(upstream_to_downstream_, &encoder_);
  EXPECT_CALL(upstream_to_downstream_,
              onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, ""));
  upstream.onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, "");
}

TEST_F(HttpUpstreamTest, RuntimeGuardDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.router_reset_with_connection_failure_reason", "false"}});
  HttpUpstream upstream(upstream_to_downstream_, &encoder_, &connection_info_);
  EXPECT_CALL(upstream_to_downstream_,
              onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, ""));
  upstream.onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, "");
}

} // namespace
} // namespace Http
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
