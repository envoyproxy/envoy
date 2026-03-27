#include "test/common/http/conn_manager_impl_test_base.h"

namespace Envoy {
namespace Http {

using testing::_;
using testing::AnyNumber;
using testing::Return;

class StreamErrorOnInvalidHttpMessageTest : public HttpConnectionManagerImplTest {
public:
  void sendInvalidRequestAndVerifyConnectionState(bool stream_error_on_invalid_http_message,
                                                  bool send_complete_request = true) {
    setup();

    EXPECT_CALL(*codec_, dispatch(_))
        .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
          decoder_ = &conn_manager_->newStream(response_encoder_);

          // These request headers are missing the necessary ":host"
          RequestHeaderMapPtr headers{
              new TestRequestHeaderMapImpl{{":method", "GET"}, {":path", "/"}}};
          decoder_->decodeHeaders(std::move(headers), send_complete_request);
          data.drain(0);
          return Http::okStatus();
        }));

    auto* filter = new MockStreamFilter();
    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> bool {
          auto factory = createStreamFilterFactoryCb(StreamFilterSharedPtr{filter});
          callbacks.setFilterConfigName("");
          factory(callbacks);
          return true;
        }));
    EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
    EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));

    // codec stream error
    EXPECT_CALL(response_encoder_, streamErrorOnInvalidHttpMessage())
        .WillOnce(Return(stream_error_on_invalid_http_message));
    EXPECT_CALL(*filter, encodeComplete());
    EXPECT_CALL(*filter, encodeHeaders(_, true));
    if (!stream_error_on_invalid_http_message) {
      EXPECT_CALL(filter_callbacks_.connection_, close(_)).Times(AnyNumber());
      if (send_complete_request) {
        // The request is complete, so we should not flush close.
        EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite))
            .Times(AnyNumber());
      } else {
        // If the request isn't complete, avoid a FIN/RST race with delay close.
        EXPECT_CALL(filter_callbacks_.connection_,
                    close(Network::ConnectionCloseType::FlushWriteAndDelay))
            .Times(AnyNumber());
      }
    }
    EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
        .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
          EXPECT_EQ("400", headers.getStatusValue());
          EXPECT_EQ("missing_host_header",
                    filter->decoder_callbacks_->streamInfo().responseCodeDetails().value());
          if (!stream_error_on_invalid_http_message) {
            EXPECT_NE(nullptr, headers.Connection());
            EXPECT_EQ("close", headers.getConnectionValue());
          } else {
            EXPECT_EQ(nullptr, headers.Connection());
          }
        }));

    EXPECT_CALL(*filter, onStreamComplete());
    EXPECT_CALL(*filter, onDestroy());

    Buffer::OwnedImpl fake_input;
    conn_manager_->onData(fake_input, false);
  }
};

TEST_F(StreamErrorOnInvalidHttpMessageTest, ConnectionTerminatedIfCodecStreamErrorIsFalse) {
  sendInvalidRequestAndVerifyConnectionState(false);
}

TEST_F(StreamErrorOnInvalidHttpMessageTest,
       ConnectionTerminatedWithDelayIfCodecStreamErrorIsFalse) {
  // Same as above, only with an incomplete request.
  sendInvalidRequestAndVerifyConnectionState(false, false);
}

TEST_F(StreamErrorOnInvalidHttpMessageTest, ConnectionOpenIfCodecStreamErrorIsTrue) {
  sendInvalidRequestAndVerifyConnectionState(true);
}

class HttpConnectionManagerImplDeathTest : public HttpConnectionManagerImplTest {
public:
  Router::RouteConfigProvider* routeConfigProvider() override {
    return route_config_provider2_.get();
  }
  Config::ConfigProvider* scopedRouteConfigProvider() override {
    return scoped_route_config_provider2_.get();
  }
  OptRef<const Router::ScopeKeyBuilder> scopeKeyBuilder() override {
    return scope_key_builder2_ ? *scope_key_builder2_ : OptRef<const Router::ScopeKeyBuilder>{};
  }

  std::shared_ptr<Router::MockRouteConfigProvider> route_config_provider2_;
  std::shared_ptr<Router::MockScopedRouteConfigProvider> scoped_route_config_provider2_;
  std::unique_ptr<Router::MockScopeKeyBuilder> scope_key_builder2_;
};

// HCM config can only have either RouteConfigProvider or ScopedRoutesConfigProvider.
TEST_F(HttpConnectionManagerImplDeathTest, InvalidConnectionManagerConfig) {
  setup();

  Buffer::OwnedImpl fake_input("1234");
  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> Http::Status {
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));
  // Either RDS or SRDS should be set.
  EXPECT_DEBUG_DEATH(conn_manager_->onData(fake_input, false),
                     "Either routeConfigProvider or \\(scopedRouteConfigProvider and "
                     "scopeKeyBuilder\\) should be set in ConnectionManagerImpl.");

  route_config_provider2_ = std::make_shared<NiceMock<Router::MockRouteConfigProvider>>();

  // Only route config provider valid.
  EXPECT_NO_THROW(conn_manager_->onData(fake_input, false));

  scoped_route_config_provider2_ =
      std::make_shared<NiceMock<Router::MockScopedRouteConfigProvider>>();
  scope_key_builder2_ = std::make_unique<NiceMock<Router::MockScopeKeyBuilder>>();
  // Can't have RDS and SRDS provider in the same time.
  EXPECT_DEBUG_DEATH(conn_manager_->onData(fake_input, false),
                     "Either routeConfigProvider or \\(scopedRouteConfigProvider and "
                     "scopeKeyBuilder\\) should be set in ConnectionManagerImpl.");

  route_config_provider2_.reset();
  // Only scoped route config provider valid.
  EXPECT_NO_THROW(conn_manager_->onData(fake_input, false));
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

} // namespace Http
} // namespace Envoy
