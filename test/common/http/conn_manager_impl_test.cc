#include "test/common/http/conn_manager_impl_test_base.h"
#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"

using testing::_;
using testing::An;
using testing::AnyNumber;
using testing::AtLeast;
using testing::Eq;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Http {

TEST_F(HttpConnectionManagerImplTest, HeaderOnlyRequestAndResponse) {
  setup(false, "envoy-custom-server", false);

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .Times(2)
      .WillRepeatedly(Invoke([&](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.ForwardedFor());
        EXPECT_EQ("http", headers.getForwardedProtoValue());
        if (headers.Path()->value() == "/healthcheck") {
          filter->callbacks_->streamInfo().healthCheck(true);
        }

        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_)).Times(2);

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(2);

  // When dispatch is called on the codec, we pretend to get a new stream and then fire a headers
  // only request into it. Then we respond into the filter.
  EXPECT_CALL(*codec_, dispatch(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        // Test not charging stats on the second call.
        if (data.length() == 4) {
          RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
              {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
          decoder_->decodeHeaders(std::move(headers), true);
        } else {
          RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
              {":authority", "host"}, {":path", "/healthcheck"}, {":method", "GET"}}};
          decoder_->decodeHeaders(std::move(headers), true);
        }

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

        // Drain 2 so that on the 2nd iteration we will hit zero.
        data.drain(2);
        return Http::okStatus();
      }));

  // Kick off the incoming data. Use extra data which should cause a redispatch.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_2xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_2xx_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_completed_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_completed_.value());
}

TEST_F(HttpConnectionManagerImplTest, 100ContinueResponse) {
  proxy_100_continue_ = true;
  setup(false, "envoy-custom-server", false);

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillRepeatedly(Invoke([&](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.ForwardedFor());
        EXPECT_EQ("http", headers.getForwardedProtoValue());
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  // When dispatch is called on the codec, we pretend to get a new stream and then fire a headers
  // only request into it. Then we respond into the filter.
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        // Test not charging stats on the second call.
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr continue_headers{new TestResponseHeaderMapImpl{{":status", "100"}}};
        filter->callbacks_->encode100ContinueHeaders(std::move(continue_headers));
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

        data.drain(4);
        return Http::okStatus();
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_1xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_1xx_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_2xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_2xx_.value());
  EXPECT_EQ(2U, stats_.named_.downstream_rq_completed_.value());
  EXPECT_EQ(2U, listener_stats_.downstream_rq_completed_.value());
}

TEST_F(HttpConnectionManagerImplTest, 100ContinueResponseWithEncoderFiltersProxyingDisabled) {
  proxy_100_continue_ = false;
  setup(false, "envoy-custom-server", false);
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Akin to 100ContinueResponseWithEncoderFilters below, but with
  // proxy_100_continue_ false. Verify the filters do not get the 100 continue
  // headers.
  EXPECT_CALL(*encoder_filters_[0], encode100ContinueHeaders(_)).Times(0);
  EXPECT_CALL(*encoder_filters_[1], encode100ContinueHeaders(_)).Times(0);
  EXPECT_CALL(response_encoder_, encode100ContinueHeaders(_)).Times(0);
  ResponseHeaderMapPtr continue_headers{new TestResponseHeaderMapImpl{{":status", "100"}}};
  decoder_filters_[0]->callbacks_->encode100ContinueHeaders(std::move(continue_headers));

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false, "details");

  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, 100ContinueResponseWithEncoderFilters) {
  proxy_100_continue_ = true;
  setup(false, "envoy-custom-server", false);
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  EXPECT_CALL(*encoder_filters_[0], encode100ContinueHeaders(_))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encode100ContinueHeaders(_))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encode100ContinueHeaders(_));
  ResponseHeaderMapPtr continue_headers{new TestResponseHeaderMapImpl{{":status", "100"}}};
  decoder_filters_[0]->callbacks_->encode100ContinueHeaders(std::move(continue_headers));

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false, "details");

  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, PauseResume100Continue) {
  proxy_100_continue_ = true;
  setup(false, "envoy-custom-server", false);
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Stop the 100-Continue at encoder filter 1. Encoder filter 0 should not yet receive the
  // 100-Continue
  EXPECT_CALL(*encoder_filters_[1], encode100ContinueHeaders(_))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[0], encode100ContinueHeaders(_)).Times(0);
  EXPECT_CALL(response_encoder_, encode100ContinueHeaders(_)).Times(0);
  ResponseHeaderMapPtr continue_headers{new TestResponseHeaderMapImpl{{":status", "100"}}};
  decoder_filters_[1]->callbacks_->encode100ContinueHeaders(std::move(continue_headers));

  // Have the encoder filter 1 continue. Make sure the 100-Continue is resumed as expected.
  EXPECT_CALL(*encoder_filters_[0], encode100ContinueHeaders(_))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encode100ContinueHeaders(_));
  encoder_filters_[1]->callbacks_->continueEncoding();

  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[1]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[1]->callbacks_->encodeHeaders(std::move(response_headers), false, "details");

  doRemoteClose();
}

// Regression test for https://github.com/envoyproxy/envoy/issues/10923.
TEST_F(HttpConnectionManagerImplTest, 100ContinueResponseWithDecoderPause) {
  proxy_100_continue_ = true;
  setup(false, "envoy-custom-server", false);

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  // Allow headers to pass.
  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillRepeatedly(
          InvokeWithoutArgs([]() -> FilterHeadersStatus { return FilterHeadersStatus::Continue; }));
  // Pause and then resume the decode pipeline, this is key to triggering #10923.
  EXPECT_CALL(*filter, decodeData(_, false)).WillOnce(InvokeWithoutArgs([]() -> FilterDataStatus {
    return FilterDataStatus::StopIterationAndBuffer;
  }));
  EXPECT_CALL(*filter, decodeData(_, true))
      .WillRepeatedly(
          InvokeWithoutArgs([]() -> FilterDataStatus { return FilterDataStatus::Continue; }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        // Test not charging stats on the second call.
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
        decoder_->decodeHeaders(std::move(headers), false);
        // Allow the decode pipeline to pause.
        decoder_->decodeData(data, false);

        ResponseHeaderMapPtr continue_headers{new TestResponseHeaderMapImpl{{":status", "100"}}};
        filter->callbacks_->encode100ContinueHeaders(std::move(continue_headers));

        // Resume decode pipeline after encoding 100 continue headers, we're now
        // ready to trigger #10923.
        decoder_->decodeData(data, true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

        data.drain(4);
        return Http::okStatus();
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_1xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_1xx_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_2xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_2xx_.value());
  EXPECT_EQ(2U, stats_.named_.downstream_rq_completed_.value());
  EXPECT_EQ(2U, listener_stats_.downstream_rq_completed_.value());
}

// By default, Envoy will set the server header to the server name, here "custom-value"
TEST_F(HttpConnectionManagerImplTest, ServerHeaderOverwritten) {
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const ResponseHeaderMap* altered_headers = sendResponseHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}, {"server", "foo"}}});
  EXPECT_EQ("custom-value", altered_headers->getServerValue());

  doRemoteClose();
}

// When configured APPEND_IF_ABSENT if the server header is present it will be retained.
TEST_F(HttpConnectionManagerImplTest, ServerHeaderAppendPresent) {
  server_transformation_ = HttpConnectionManagerProto::APPEND_IF_ABSENT;
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const ResponseHeaderMap* altered_headers = sendResponseHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}, {"server", "foo"}}});
  EXPECT_EQ("foo", altered_headers->getServerValue());

  doRemoteClose();
}

// When configured APPEND_IF_ABSENT if the server header is absent the server name will be set.
TEST_F(HttpConnectionManagerImplTest, ServerHeaderAppendAbsent) {
  server_transformation_ = HttpConnectionManagerProto::APPEND_IF_ABSENT;
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const ResponseHeaderMap* altered_headers =
      sendResponseHeaders(ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}});
  EXPECT_EQ("custom-value", altered_headers->getServerValue());

  doRemoteClose();
}

// When configured PASS_THROUGH, the server name will pass through.
TEST_F(HttpConnectionManagerImplTest, ServerHeaderPassthroughPresent) {
  server_transformation_ = HttpConnectionManagerProto::PASS_THROUGH;
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const ResponseHeaderMap* altered_headers = sendResponseHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}, {"server", "foo"}}});
  EXPECT_EQ("foo", altered_headers->getServerValue());

  doRemoteClose();
}

// When configured PASS_THROUGH, the server header will not be added if absent.
TEST_F(HttpConnectionManagerImplTest, ServerHeaderPassthroughAbsent) {
  server_transformation_ = HttpConnectionManagerProto::PASS_THROUGH;
  setup(false, "custom-value", false);
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const ResponseHeaderMap* altered_headers =
      sendResponseHeaders(ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}});
  EXPECT_TRUE(altered_headers->Server() == nullptr);

  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, InvalidPathWithDualFilter) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":path", "http://api.lyft.com/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  // This test also verifies that decoder/encoder filters have onDestroy() called only once.
  auto* filter = new MockStreamFilter();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamFilter(StreamFilterSharedPtr{filter});
      }));
  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));

  EXPECT_CALL(*filter, encodeHeaders(_, true));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("404", headers.getStatusValue());
        EXPECT_EQ("absolute_path_rejected",
                  filter->decoder_callbacks_->streamInfo().responseCodeDetails().value());
      }));
  EXPECT_CALL(*filter, onStreamComplete());
  EXPECT_CALL(*filter, onDestroy());

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Invalid paths are rejected with 400.
TEST_F(HttpConnectionManagerImplTest, PathFailedtoSanitize) {
  InSequence s;
  setup(false, "");
  // Enable path sanitizer
  normalize_path_ = true;

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"},
        {":path", "/ab%00c"}, // "%00" is not valid in path according to RFC
        {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));
  EXPECT_CALL(response_encoder_, streamErrorOnInvalidHttpMessage()).WillOnce(Return(true));

  // This test also verifies that decoder/encoder filters have onDestroy() called only once.
  auto* filter = new MockStreamFilter();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamFilter(StreamFilterSharedPtr{filter});
      }));
  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter, encodeHeaders(_, true));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("400", headers.getStatusValue());
        EXPECT_EQ("path_normalization_failed",
                  filter->decoder_callbacks_->streamInfo().responseCodeDetails().value());
      }));
  EXPECT_CALL(*filter, onStreamComplete());
  EXPECT_CALL(*filter, onDestroy());

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Filters observe normalized paths, not the original path, when path
// normalization is configured.
TEST_F(HttpConnectionManagerImplTest, FilterShouldUseSantizedPath) {
  setup(false, "");
  // Enable path sanitizer
  normalize_path_ = true;
  const std::string original_path = "/x/%2E%2e/z";
  const std::string normalized_path = "/z";

  auto* filter = new MockStreamFilter();

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillRepeatedly(Invoke([&](RequestHeaderMap& header_map, bool) -> FilterHeadersStatus {
        EXPECT_EQ(normalized_path, header_map.getPathValue());
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":path", original_path}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*filter, onStreamComplete());
  EXPECT_CALL(*filter, onDestroy());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// The router observes normalized paths, not the original path, when path
// normalization is configured.
TEST_F(HttpConnectionManagerImplTest, RouteShouldUseSantizedPath) {
  setup(false, "");
  // Enable path sanitizer
  normalize_path_ = true;
  const std::string original_path = "/x/%2E%2e/z";
  const std::string normalized_path = "/z";

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":path", original_path}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  const std::string fake_cluster_name = "fake_cluster";

  std::shared_ptr<Upstream::MockThreadLocalCluster> fake_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  std::shared_ptr<Router::MockRoute> route = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(route->route_entry_, clusterName()).WillRepeatedly(ReturnRef(fake_cluster_name));

  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
      .WillOnce(Invoke([&](const Router::RouteCallback&, const Http::RequestHeaderMap& header_map,
                           const StreamInfo::StreamInfo&, uint64_t) {
        EXPECT_EQ(normalized_path, header_map.getPathValue());
        return route;
      }));
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks&) -> void {}));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Clean up.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RouteOverride) {
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  setupFilterChain(2, 0);
  const std::string foo_bar_baz_cluster_name = "foo_bar_baz";
  const std::string foo_bar_cluster_name = "foo_bar";
  const std::string foo_cluster_name = "foo";
  const std::string default_cluster_name = "default";

  std::shared_ptr<Upstream::MockThreadLocalCluster> foo_bar_baz_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();

  std::shared_ptr<Upstream::MockThreadLocalCluster> foo_bar_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, get(absl::string_view{foo_bar_cluster_name}))
      .WillOnce(Return(foo_bar_cluster.get()));

  std::shared_ptr<Upstream::MockThreadLocalCluster> foo_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();

  std::shared_ptr<Upstream::MockThreadLocalCluster> default_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, get(absl::string_view{default_cluster_name}))
      .Times(2)
      .WillRepeatedly(Return(default_cluster.get()));

  std::shared_ptr<Router::MockRoute> foo_bar_baz_route =
      std::make_shared<NiceMock<Router::MockRoute>>();

  std::shared_ptr<Router::MockRoute> foo_bar_route =
      std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(foo_bar_route->route_entry_, clusterName()).WillOnce(ReturnRef(foo_bar_cluster_name));

  std::shared_ptr<Router::MockRoute> foo_route = std::make_shared<NiceMock<Router::MockRoute>>();

  std::shared_ptr<Router::MockRoute> default_route =
      std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(default_route->route_entry_, clusterName())
      .Times(2)
      .WillRepeatedly(ReturnRef(default_cluster_name));

  using ::testing::InSequence;
  {
    InSequence seq;
    EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
        .WillOnce(Return(default_route));

    // This filter iterates through all possible route matches and choose the last matched route
    EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
        .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
          EXPECT_EQ(default_route, decoder_filters_[0]->callbacks_->route());
          EXPECT_EQ(default_route->routeEntry(),
                    decoder_filters_[0]->callbacks_->streamInfo().routeEntry());
          EXPECT_EQ(default_cluster->info(), decoder_filters_[0]->callbacks_->clusterInfo());

          // Not clearing cached route returns cached route and doesn't invoke cb.
          Router::RouteConstSharedPtr route = decoder_filters_[0]->callbacks_->route(
              [](Router::RouteConstSharedPtr, Router::RouteEvalStatus) -> Router::RouteMatchStatus {
                ADD_FAILURE() << "When route cache is not cleared CB should not be invoked";
                return Router::RouteMatchStatus::Accept;
              });
          EXPECT_EQ(default_route, route);

          int ctr = 0;
          const Router::RouteCallback& cb =
              [&](Router::RouteConstSharedPtr route,
                  Router::RouteEvalStatus route_eval_status) -> Router::RouteMatchStatus {
            EXPECT_LE(ctr, 3);
            if (ctr == 0) {
              ++ctr;
              EXPECT_EQ(foo_bar_baz_route, route);
              EXPECT_EQ(route_eval_status, Router::RouteEvalStatus::HasMoreRoutes);
              return Router::RouteMatchStatus::Continue;
            }

            if (ctr == 1) {
              ++ctr;
              EXPECT_EQ(foo_bar_route, route);
              EXPECT_EQ(route_eval_status, Router::RouteEvalStatus::HasMoreRoutes);
              return Router::RouteMatchStatus::Continue;
            }

            if (ctr == 2) {
              ++ctr;
              EXPECT_EQ(foo_route, route);
              EXPECT_EQ(route_eval_status, Router::RouteEvalStatus::HasMoreRoutes);
              return Router::RouteMatchStatus::Continue;
            }

            if (ctr == 3) {
              ++ctr;
              EXPECT_EQ(default_route, route);
              EXPECT_EQ(route_eval_status, Router::RouteEvalStatus::NoMoreRoutes);
              return Router::RouteMatchStatus::Accept;
            }
            return Router::RouteMatchStatus::Accept;
          };

          decoder_filters_[0]->callbacks_->clearRouteCache();
          route = decoder_filters_[0]->callbacks_->route(cb);

          EXPECT_EQ(default_route, route);
          EXPECT_EQ(default_route, decoder_filters_[0]->callbacks_->route());
          EXPECT_EQ(default_route->routeEntry(),
                    decoder_filters_[0]->callbacks_->streamInfo().routeEntry());
          EXPECT_EQ(default_cluster->info(), decoder_filters_[0]->callbacks_->clusterInfo());

          return FilterHeadersStatus::Continue;
        }));

    // This route config expected to be invoked for all matching routes
    EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
        .WillOnce(Invoke([&](const Router::RouteCallback& cb, const Http::RequestHeaderMap&,
                             const Envoy::StreamInfo::StreamInfo&,
                             uint64_t) -> Router::RouteConstSharedPtr {
          EXPECT_EQ(cb(foo_bar_baz_route, Router::RouteEvalStatus::HasMoreRoutes),
                    Router::RouteMatchStatus::Continue);
          EXPECT_EQ(cb(foo_bar_route, Router::RouteEvalStatus::HasMoreRoutes),
                    Router::RouteMatchStatus::Continue);
          EXPECT_EQ(cb(foo_route, Router::RouteEvalStatus::HasMoreRoutes),
                    Router::RouteMatchStatus::Continue);
          EXPECT_EQ(cb(default_route, Router::RouteEvalStatus::NoMoreRoutes),
                    Router::RouteMatchStatus::Accept);
          return default_route;
        }));

    EXPECT_CALL(*decoder_filters_[0], decodeComplete());

    // This filter chooses second route
    EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
        .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
          EXPECT_EQ(default_route, decoder_filters_[1]->callbacks_->route());
          EXPECT_EQ(default_route->routeEntry(),
                    decoder_filters_[1]->callbacks_->streamInfo().routeEntry());
          EXPECT_EQ(default_cluster->info(), decoder_filters_[1]->callbacks_->clusterInfo());

          int ctr = 0;
          const Router::RouteCallback& cb =
              [&](Router::RouteConstSharedPtr route,
                  Router::RouteEvalStatus route_eval_status) -> Router::RouteMatchStatus {
            EXPECT_LE(ctr, 1);
            if (ctr == 0) {
              ++ctr;
              EXPECT_EQ(foo_bar_baz_route, route);
              EXPECT_EQ(route_eval_status, Router::RouteEvalStatus::HasMoreRoutes);
              return Router::RouteMatchStatus::Continue;
            }

            if (ctr == 1) {
              ++ctr;
              EXPECT_EQ(foo_bar_route, route);
              EXPECT_EQ(route_eval_status, Router::RouteEvalStatus::HasMoreRoutes);
              return Router::RouteMatchStatus::Accept;
            }
            return Router::RouteMatchStatus::Accept;
          };

          decoder_filters_[0]->callbacks_->clearRouteCache();
          decoder_filters_[1]->callbacks_->route(cb);

          EXPECT_EQ(foo_bar_route, decoder_filters_[1]->callbacks_->route());
          EXPECT_EQ(foo_bar_route->routeEntry(),
                    decoder_filters_[1]->callbacks_->streamInfo().routeEntry());
          EXPECT_EQ(foo_bar_cluster->info(), decoder_filters_[1]->callbacks_->clusterInfo());

          return FilterHeadersStatus::Continue;
        }));

    // This route config expected to be invoked for first two matching routes
    EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
        .WillOnce(Invoke([&](const Router::RouteCallback& cb, const Http::RequestHeaderMap&,
                             const Envoy::StreamInfo::StreamInfo&,
                             uint64_t) -> Router::RouteConstSharedPtr {
          EXPECT_EQ(cb(foo_bar_baz_route, Router::RouteEvalStatus::HasMoreRoutes),
                    Router::RouteMatchStatus::Continue);
          EXPECT_EQ(cb(foo_bar_route, Router::RouteEvalStatus::HasMoreRoutes),
                    Router::RouteMatchStatus::Accept);
          return foo_bar_route;
        }));

    EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  }

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Clean up.
  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Filters observe host header w/o port's part when port's removal is configured
TEST_F(HttpConnectionManagerImplTest, FilterShouldUseNormalizedHost) {
  setup(false, "");
  // Enable port removal
  strip_matching_port_ = true;
  const std::string original_host = "host:443";
  const std::string normalized_host = "host";

  auto* filter = new MockStreamFilter();

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillRepeatedly(Invoke([&](RequestHeaderMap& header_map, bool) -> FilterHeadersStatus {
        EXPECT_EQ(normalized_host, header_map.getHostValue());
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", original_host}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Clean up.
  EXPECT_CALL(*filter, onStreamComplete());
  EXPECT_CALL(*filter, onDestroy());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// The router observes host header w/o port, not the original host, when
// remove_port is configured
TEST_F(HttpConnectionManagerImplTest, RouteShouldUseNormalizedHost) {
  setup(false, "");
  // Enable port removal
  strip_matching_port_ = true;
  const std::string original_host = "host:443";
  const std::string normalized_host = "host";

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", original_host}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  const std::string fake_cluster_name = "fake_cluster";

  std::shared_ptr<Upstream::MockThreadLocalCluster> fake_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  std::shared_ptr<Router::MockRoute> route = std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(route->route_entry_, clusterName()).WillRepeatedly(ReturnRef(fake_cluster_name));

  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
      .WillOnce(Invoke([&](const Router::RouteCallback&, const Http::RequestHeaderMap& header_map,
                           const StreamInfo::StreamInfo&, uint64_t) {
        EXPECT_EQ(normalized_host, header_map.getHostValue());
        return route;
      }));
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks&) -> void {}));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Clean up.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, PreserveUpstreamDateDisabledDateNotSet) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.preserve_upstream_date", "false"}});
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();
  const auto* modified_headers = sendResponseHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}, {"server", "foo"}}});
  ASSERT_TRUE(modified_headers);
  EXPECT_TRUE(modified_headers->Date());
  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, PreserveUpstreamDateEnabledDateNotSet) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.preserve_upstream_date", "true"}});
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();
  const auto* modified_headers = sendResponseHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}, {"server", "foo"}}});
  ASSERT_TRUE(modified_headers);
  EXPECT_TRUE(modified_headers->Date());
  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, PreserveUpstreamDateDisabledDateSet) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.preserve_upstream_date", "false"}});
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();
  const std::string expected_date{"Tue, 15 Nov 1994 08:12:31 GMT"};
  const auto* modified_headers =
      sendResponseHeaders(ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{
          {":status", "200"}, {"server", "foo"}, {"date", expected_date.c_str()}}});
  ASSERT_TRUE(modified_headers);
  ASSERT_TRUE(modified_headers->Date());
  EXPECT_NE(expected_date, modified_headers->getDateValue());
  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, PreserveUpstreamDateEnabledDateSet) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.preserve_upstream_date", "true"}});
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();
  const std::string expected_date{"Tue, 15 Nov 1994 08:12:31 GMT"};
  const auto* modified_headers =
      sendResponseHeaders(ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{
          {":status", "200"}, {"server", "foo"}, {"date", expected_date.c_str()}}});
  ASSERT_TRUE(modified_headers);
  ASSERT_TRUE(modified_headers->Date());
  EXPECT_EQ(expected_date, modified_headers->getDateValue());
  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, PreserveUpstreamDateDisabledDateFromCache) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.preserve_upstream_date", "false"}});
  setup(false, "");
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();
  encoder_filters_[0]->callbacks_->streamInfo().setResponseFlag(
      StreamInfo::ResponseFlag::ResponseFromCacheFilter);
  const std::string expected_date{"Tue, 15 Nov 1994 08:12:31 GMT"};
  const auto* modified_headers =
      sendResponseHeaders(ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{
          {":status", "200"}, {"server", "foo"}, {"date", expected_date.c_str()}}});
  ASSERT_TRUE(modified_headers);
  ASSERT_TRUE(modified_headers->Date());
  EXPECT_EQ(expected_date, modified_headers->getDateValue());
  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlow) {
  setup(false, "");

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

            return span;
          }));
  // No decorator.
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator())
      .WillRepeatedly(Return(nullptr));
  envoy::type::v3::FractionalPercent percent1;
  percent1.set_numerator(100);
  envoy::type::v3::FractionalPercent percent2;
  percent2.set_numerator(10000);
  percent2.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);

  struct TracingTagMetaSuite {
    using Factory =
        std::function<Tracing::CustomTagConstSharedPtr(const std::string&, const std::string&)>;
    std::string prefix;
    Factory factory;
  };
  struct TracingTagSuite {
    bool has_conn;
    bool has_route;
    std::list<Tracing::CustomTagConstSharedPtr> custom_tags;
    std::string tag;
    std::string tag_value;
  };
  std::vector<TracingTagMetaSuite> tracing_tag_meta_cases = {
      {"l-tag",
       [](const std::string& t, const std::string& v) {
         envoy::type::tracing::v3::CustomTag::Literal literal;
         literal.set_value(v);
         return std::make_shared<Tracing::LiteralCustomTag>(t, literal);
       }},
      {"e-tag",
       [](const std::string& t, const std::string& v) {
         envoy::type::tracing::v3::CustomTag::Environment e;
         e.set_default_value(v);
         return std::make_shared<Tracing::EnvironmentCustomTag>(t, e);
       }},
      {"x-tag",
       [](const std::string& t, const std::string& v) {
         envoy::type::tracing::v3::CustomTag::Header h;
         h.set_default_value(v);
         return std::make_shared<Tracing::RequestHeaderCustomTag>(t, h);
       }},
      {"m-tag", [](const std::string& t, const std::string& v) {
         envoy::type::tracing::v3::CustomTag::Metadata m;
         m.mutable_kind()->mutable_host();
         m.set_default_value(v);
         return std::make_shared<Tracing::MetadataCustomTag>(t, m);
       }}};
  std::vector<TracingTagSuite> tracing_tag_cases;
  for (const TracingTagMetaSuite& ms : tracing_tag_meta_cases) {
    const std::string& t1 = ms.prefix + "-1";
    const std::string& v1 = ms.prefix + "-v1";
    tracing_tag_cases.push_back({true, false, {ms.factory(t1, v1)}, t1, v1});

    const std::string& t2 = ms.prefix + "-2";
    const std::string& v2 = ms.prefix + "-v2";
    const std::string& rv2 = ms.prefix + "-r2";
    tracing_tag_cases.push_back({true, true, {ms.factory(t2, v2), ms.factory(t2, rv2)}, t2, rv2});

    const std::string& t3 = ms.prefix + "-3";
    const std::string& rv3 = ms.prefix + "-r3";
    tracing_tag_cases.push_back({false, true, {ms.factory(t3, rv3)}, t3, rv3});
  }
  Tracing::CustomTagMap conn_tracing_tags = {
      {":method", requestHeaderCustomTag(":method")}}; // legacy test case
  Tracing::CustomTagMap route_tracing_tags;
  for (TracingTagSuite& s : tracing_tag_cases) {
    if (s.has_conn) {
      const Tracing::CustomTagConstSharedPtr& ptr = s.custom_tags.front();
      conn_tracing_tags.emplace(ptr->tag(), ptr);
      s.custom_tags.pop_front();
    }
    if (s.has_route) {
      const Tracing::CustomTagConstSharedPtr& ptr = s.custom_tags.front();
      route_tracing_tags.emplace(ptr->tag(), ptr);
      s.custom_tags.pop_front();
    }
  }
  tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
      TracingConnectionManagerConfig{Tracing::OperationName::Ingress, conn_tracing_tags, percent1,
                                     percent2, percent1, false, 256});
  NiceMock<Router::MockRouteTracing> route_tracing;
  ON_CALL(route_tracing, getClientSampling()).WillByDefault(ReturnRef(percent1));
  ON_CALL(route_tracing, getRandomSampling()).WillByDefault(ReturnRef(percent2));
  ON_CALL(route_tracing, getOverallSampling()).WillByDefault(ReturnRef(percent1));
  ON_CALL(route_tracing, getCustomTags()).WillByDefault(ReturnRef(route_tracing_tags));
  ON_CALL(*route_config_provider_.route_config_->route_, tracingConfig())
      .WillByDefault(Return(&route_tracing));

  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  // Verify tag is set based on the request headers.
  EXPECT_CALL(*span, setTag(Eq(":method"), Eq("GET")));
  for (const TracingTagSuite& s : tracing_tag_cases) {
    EXPECT_CALL(*span, setTag(Eq(s.tag), Eq(s.tag_value)));
  }
  // Verify if the activeSpan interface returns reference to the current span.
  EXPECT_CALL(*span, setTag(Eq("service-cluster"), Eq("scoobydoo")));
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*span, setOperation(_)).Times(0);

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");
        data.drain(4);
        return Http::okStatus();
      }));

  // Should be no 'x-envoy-decorator-operation' response header.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(nullptr, headers.EnvoyDecoratorOperation());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1UL, tracing_stats_.service_forced_.value());
  EXPECT_EQ(0UL, tracing_stats_.random_sampling_.value());
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowIngressDecorator) {
  setup(false, "");

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "testOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(2);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_EQ(true, route_config_provider_.route_config_->route_->decorator_.propagate());
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*span, setOperation(_)).Times(0);

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");
        data.drain(4);
        return Http::okStatus();
      }));

  // Verify decorator operation response header has been defined.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("testOp", headers.getEnvoyDecoratorOperationValue());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowIngressDecoratorPropagateFalse) {
  setup(false, "");

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "testOp";
  ON_CALL(route_config_provider_.route_config_->route_->decorator_, propagate())
      .WillByDefault(Return(false));
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(2);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*span, setOperation(_)).Times(0);

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");
        data.drain(4);
        return Http::okStatus();
      }));

  // Verify decorator operation response header has NOT been defined (i.e. not propagated).
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(nullptr, headers.EnvoyDecoratorOperation());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowIngressDecoratorOverrideOp) {
  setup(false, "");

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Ingress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "initOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(2);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*span, setOperation(Eq("testOp")));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"},
                                         {"x-envoy-decorator-operation", "testOp"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");

        data.drain(4);
        return Http::okStatus();
      }));

  // Should be no 'x-envoy-decorator-operation' response header, as decorator
  // was overridden by request header.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ(nullptr, headers.EnvoyDecoratorOperation());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowEgressDecorator) {
  setup(false, "");
  envoy::type::v3::FractionalPercent percent1;
  percent1.set_numerator(100);
  envoy::type::v3::FractionalPercent percent2;
  percent2.set_numerator(10000);
  percent2.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
  tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
      TracingConnectionManagerConfig{Tracing::OperationName::Egress,
                                     {{":method", requestHeaderCustomTag(":method")}},
                                     percent1,
                                     percent2,
                                     percent1,
                                     false,
                                     256});

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Egress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "testOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(2);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_EQ(true, route_config_provider_.route_config_->route_->decorator_.propagate());
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*span, setOperation(_)).Times(0);

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");

        data.drain(4);
        return Http::okStatus();
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.EnvoyDecoratorOperation());
        // Verify that decorator operation has been set as request header.
        EXPECT_EQ("testOp", headers.getEnvoyDecoratorOperationValue());
        return FilterHeadersStatus::StopIteration;
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowEgressDecoratorPropagateFalse) {
  setup(false, "");
  envoy::type::v3::FractionalPercent percent1;
  percent1.set_numerator(100);
  envoy::type::v3::FractionalPercent percent2;
  percent2.set_numerator(10000);
  percent2.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
  tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
      TracingConnectionManagerConfig{Tracing::OperationName::Egress,
                                     {{":method", requestHeaderCustomTag(":method")}},
                                     percent1,
                                     percent2,
                                     percent1,
                                     false,
                                     256});

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Egress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "testOp";
  ON_CALL(route_config_provider_.route_config_->route_->decorator_, propagate())
      .WillByDefault(Return(false));
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(2);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*span, setOperation(_)).Times(0);

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");

        data.drain(4);
        return Http::okStatus();
      }));

  // Verify that decorator operation has NOT been set as request header (propagate is false)
  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_EQ(nullptr, headers.EnvoyDecoratorOperation());
        return FilterHeadersStatus::StopIteration;
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlowEgressDecoratorOverrideOp) {
  setup(false, "");
  envoy::type::v3::FractionalPercent percent1;
  percent1.set_numerator(100);
  envoy::type::v3::FractionalPercent percent2;
  percent2.set_numerator(10000);
  percent2.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
  tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
      TracingConnectionManagerConfig{Tracing::OperationName::Egress,
                                     {{":method", requestHeaderCustomTag(":method")}},
                                     percent1,
                                     percent2,
                                     percent1,
                                     false,
                                     256});

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, const HeaderMap&, const StreamInfo::StreamInfo&,
                     const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Egress, config.operationName());

            return span;
          }));
  route_config_provider_.route_config_->route_->decorator_.operation_ = "initOp";
  EXPECT_CALL(*route_config_provider_.route_config_->route_, decorator()).Times(2);
  EXPECT_CALL(route_config_provider_.route_config_->route_->decorator_, apply(_))
      .WillOnce(Invoke(
          [&](const Tracing::Span& apply_to_span) -> void { EXPECT_EQ(span, &apply_to_span); }));
  EXPECT_CALL(*span, finishSpan());
  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(true));
  // Verify that span operation overridden by value supplied in response header.
  EXPECT_CALL(*span, setOperation(Eq("testOp")));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{
            {":status", "200"}, {"x-envoy-decorator-operation", "testOp"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
        filter->callbacks_->activeSpan().setTag("service-cluster", "scoobydoo");

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest,
       StartAndFinishSpanNormalFlowEgressDecoratorOverrideOpNoActiveSpan) {
  setup(false, "");
  envoy::type::v3::FractionalPercent percent1;
  percent1.set_numerator(100);
  envoy::type::v3::FractionalPercent percent2;
  percent2.set_numerator(10000);
  percent2.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
  tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
      TracingConnectionManagerConfig{Tracing::OperationName::Egress,
                                     {{":method", requestHeaderCustomTag(":method")}},
                                     percent1,
                                     percent2,
                                     percent1,
                                     false,
                                     256});

  EXPECT_CALL(
      runtime_.snapshot_,
      featureEnabled("tracing.global_enabled", An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillOnce(Return(false));
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  // Treat request as internal, otherwise x-request-id header will be overwritten.
  use_remote_address_ = false;
  EXPECT_CALL(random_, uuid()).Times(0);

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{
            {":status", "200"}, {"x-envoy-decorator-operation", "testOp"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestAccessLog) {
  static constexpr char local_address[] = "0.0.0.0";
  static constexpr char xff_address[] = "1.2.3.4";

  // stream_info.downstreamRemoteAddress will infer the address from request
  // headers instead of the physical connection
  use_remote_address_ = false;
  setup(false, "");

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
        callbacks.addAccessLogHandler(handler);
      }));

  EXPECT_CALL(*handler, log(_, _, _, _))
      .WillOnce(Invoke([](const HeaderMap*, const HeaderMap*, const HeaderMap*,
                          const StreamInfo::StreamInfo& stream_info) {
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_EQ(stream_info.responseCode().value(), uint32_t(200));
        EXPECT_NE(nullptr, stream_info.downstreamLocalAddress());
        EXPECT_NE(nullptr, stream_info.downstreamRemoteAddress());
        EXPECT_NE(nullptr, stream_info.downstreamDirectRemoteAddress());
        EXPECT_NE(nullptr, stream_info.routeEntry());

        EXPECT_EQ(stream_info.downstreamRemoteAddress()->ip()->addressAsString(), xff_address);
        EXPECT_EQ(stream_info.downstreamDirectRemoteAddress()->ip()->addressAsString(),
                  local_address);
      }));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-forwarded-for", xff_address},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestFilterCanEnrichAccessLogs) {
  setup(false, "");

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
        callbacks.addAccessLogHandler(handler);
      }));

  EXPECT_CALL(*filter, onStreamComplete()).WillOnce(Invoke([&]() {
    ProtobufWkt::Value metadata_value;
    metadata_value.set_string_value("value");
    ProtobufWkt::Struct metadata;
    metadata.mutable_fields()->insert({"field", metadata_value});
    filter->callbacks_->streamInfo().setDynamicMetadata("metadata_key", metadata);
  }));

  EXPECT_CALL(*handler, log(_, _, _, _))
      .WillOnce(Invoke([](const HeaderMap*, const HeaderMap*, const HeaderMap*,
                          const StreamInfo::StreamInfo& stream_info) {
        auto dynamic_meta = stream_info.dynamicMetadata().filter_metadata().at("metadata_key");
        EXPECT_EQ("value", dynamic_meta.fields().at("field").string_value());
      }));

  NiceMock<MockResponseEncoder> encoder;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(encoder);

        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":method", "GET"}, {":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestDownstreamDisconnectAccessLog) {
  setup(false, "");

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
        callbacks.addAccessLogHandler(handler);
      }));

  EXPECT_CALL(*handler, log(_, _, _, _))
      .WillOnce(Invoke([](const HeaderMap*, const HeaderMap*, const HeaderMap*,
                          const StreamInfo::StreamInfo& stream_info) {
        EXPECT_FALSE(stream_info.responseCode());
        EXPECT_TRUE(stream_info.hasAnyResponseFlag());
        EXPECT_TRUE(
            stream_info.hasResponseFlag(StreamInfo::ResponseFlag::DownstreamConnectionTermination));
        EXPECT_EQ("downstream_remote_disconnect", stream_info.responseCodeDetails().value());
      }));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":method", "GET"}, {":authority", "host"}, {":path", "/"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, TestAccessLogWithTrailers) {
  setup(false, "");

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
        callbacks.addAccessLogHandler(handler);
      }));

  EXPECT_CALL(*handler, log(_, _, _, _))
      .WillOnce(Invoke([](const HeaderMap*, const HeaderMap*, const HeaderMap*,
                          const StreamInfo::StreamInfo& stream_info) {
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_EQ(stream_info.responseCode().value(), uint32_t(200));
        EXPECT_NE(nullptr, stream_info.downstreamLocalAddress());
        EXPECT_NE(nullptr, stream_info.downstreamRemoteAddress());
        EXPECT_NE(nullptr, stream_info.downstreamDirectRemoteAddress());
        EXPECT_NE(nullptr, stream_info.routeEntry());
      }));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), false, "details");

        ResponseTrailerMapPtr response_trailers{new TestResponseTrailerMapImpl{{"x-trailer", "1"}}};
        filter->callbacks_->encodeTrailers(std::move(response_trailers));

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestAccessLogWithInvalidRequest) {
  setup(false, "");

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
        callbacks.addAccessLogHandler(handler);
      }));

  EXPECT_CALL(*handler, log(_, _, _, _))
      .WillOnce(Invoke([](const HeaderMap*, const HeaderMap*, const HeaderMap*,
                          const StreamInfo::StreamInfo& stream_info) {
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_EQ(stream_info.responseCode().value(), uint32_t(400));
        EXPECT_EQ("missing_host_header", stream_info.responseCodeDetails().value());
        EXPECT_NE(nullptr, stream_info.downstreamLocalAddress());
        EXPECT_NE(nullptr, stream_info.downstreamRemoteAddress());
        EXPECT_NE(nullptr, stream_info.downstreamDirectRemoteAddress());
        EXPECT_EQ(nullptr, stream_info.routeEntry());
      }));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        // These request headers are missing the necessary ":host"
        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"}, {":path", "/"}}};
        decoder_->decodeHeaders(std::move(headers), true);
        data.drain(0);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);
}

class StreamErrorOnInvalidHttpMessageTest : public HttpConnectionManagerImplTest {
public:
  void sendInvalidRequestAndVerifyConnectionState(bool stream_error_on_invalid_http_message) {
    setup(false, "");

    EXPECT_CALL(*codec_, dispatch(_))
        .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
          decoder_ = &conn_manager_->newStream(response_encoder_);

          // These request headers are missing the necessary ":host"
          RequestHeaderMapPtr headers{
              new TestRequestHeaderMapImpl{{":method", "GET"}, {":path", "/"}}};
          decoder_->decodeHeaders(std::move(headers), true);
          data.drain(0);
          return Http::okStatus();
        }));

    auto* filter = new MockStreamFilter();
    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
          callbacks.addStreamFilter(StreamFilterSharedPtr{filter});
        }));
    EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
    EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));

    // codec stream error
    EXPECT_CALL(response_encoder_, streamErrorOnInvalidHttpMessage())
        .WillOnce(Return(stream_error_on_invalid_http_message));
    EXPECT_CALL(*filter, encodeHeaders(_, true));
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

TEST_F(StreamErrorOnInvalidHttpMessageTest, ConnectionOpenIfCodecStreamErrorIsTrue) {
  sendInvalidRequestAndVerifyConnectionState(true);
}

TEST_F(HttpConnectionManagerImplTest, TestAccessLogSsl) {
  setup(true, "");

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
        callbacks.addAccessLogHandler(handler);
      }));

  EXPECT_CALL(*handler, log(_, _, _, _))
      .WillOnce(Invoke([](const HeaderMap*, const HeaderMap*, const HeaderMap*,
                          const StreamInfo::StreamInfo& stream_info) {
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_EQ(stream_info.responseCode().value(), uint32_t(200));
        EXPECT_NE(nullptr, stream_info.downstreamLocalAddress());
        EXPECT_NE(nullptr, stream_info.downstreamRemoteAddress());
        EXPECT_NE(nullptr, stream_info.downstreamDirectRemoteAddress());
        EXPECT_NE(nullptr, stream_info.downstreamSslConnection());
        EXPECT_NE(nullptr, stream_info.routeEntry());
      }));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), false, "details");

        ResponseTrailerMapPtr response_trailers{new TestResponseTrailerMapImpl{{"x-trailer", "1"}}};
        filter->callbacks_->encodeTrailers(std::move(response_trailers));

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, DoNotStartSpanIfTracingIsNotEnabled) {
  setup(false, "");

  // Disable tracing.
  tracing_config_.reset();

  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _)).Times(0);
  ON_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled",
                                             An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillByDefault(Return(true));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "GET"},
                                         {":authority", "host"},
                                         {":path", "/"},
                                         {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
        decoder_->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, NoPath) {
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":method", "NOT_CONNECT"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("404", headers.getStatusValue());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// No idle timeout when route idle timeout is implied at both global and
// per-route level. The connection manager config is responsible for managing
// the default configuration aspects.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutNotConfigured) {
  setup(false, "");

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, createTimer_(_)).Times(0);
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
        decoder_->decodeHeaders(std::move(headers), false);

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_idle_timeout_.value());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// When the global timeout is configured, the timer is enabled before we receive
// headers, if it fires we don't faceplant.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutGlobal) {
  stream_idle_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* idle_timer = setUpTimer();
    EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
    conn_manager_->newStream(response_encoder_);

    // Expect resetIdleTimer() to be called for the response
    // encodeHeaders()/encodeData().
    EXPECT_CALL(*idle_timer, enableTimer(_, _)).Times(2);
    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();
    return Http::okStatus();
  }));

  // 408 direct response after timeout.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("408", headers.getStatusValue());
      }));
  std::string response_body;
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ("stream timeout", response_body);
  EXPECT_EQ(1U, stats_.named_.downstream_rq_idle_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, AccessEncoderRouteBeforeHeadersArriveOnIdleTimeout) {
  stream_idle_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  std::shared_ptr<MockStreamEncoderFilter> filter(new NiceMock<MockStreamEncoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamEncoderFilter(filter);
      }));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* idle_timer = setUpTimer();
    EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
    conn_manager_->newStream(response_encoder_);

    // Expect resetIdleTimer() to be called for the response
    // encodeHeaders()/encodeData().
    EXPECT_CALL(*idle_timer, enableTimer(_, _)).Times(2);
    EXPECT_CALL(*idle_timer, disableTimer());
    // Simulate and idle timeout so that the filter chain gets created.
    idle_timer->invokeCallback();
    return Http::okStatus();
  }));

  // This should not be called as we don't have request headers.
  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _)).Times(0);

  EXPECT_CALL(*filter, encodeHeaders(_, _))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        // Under heavy load it is possible that stream timeout will be reached before any headers
        // were received. Envoy will create a local reply that will go through the encoder filter
        // chain. We want to make sure that encoder filters get a null route object.
        auto route = filter->callbacks_->route();
        EXPECT_EQ(route.get(), nullptr);
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*filter, encodeData(_, _));
  EXPECT_CALL(*filter, encodeComplete());

  EXPECT_CALL(*filter, onStreamComplete());
  EXPECT_CALL(*filter, onDestroy());

  EXPECT_CALL(response_encoder_, encodeHeaders(_, _));
  EXPECT_CALL(response_encoder_, encodeData(_, _));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestStreamIdleAccessLog) {
  stream_idle_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillRepeatedly(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* idle_timer = setUpTimer();
    EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
    conn_manager_->newStream(response_encoder_);

    // Expect resetIdleTimer() to be called for the response
    // encodeHeaders()/encodeData().
    EXPECT_CALL(*idle_timer, enableTimer(_, _)).Times(2);
    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();
    return Http::okStatus();
  }));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  // 408 direct response after timeout.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("408", headers.getStatusValue());
      }));

  std::string response_body;
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

  EXPECT_CALL(*handler, log(_, _, _, _))
      .WillOnce(Invoke([](const HeaderMap*, const HeaderMap*, const HeaderMap*,
                          const StreamInfo::StreamInfo& stream_info) {
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_TRUE(stream_info.hasAnyResponseFlag());
        EXPECT_TRUE(stream_info.hasResponseFlag(StreamInfo::ResponseFlag::StreamIdleTimeout));
      }));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
        callbacks.addAccessLogHandler(handler);
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ("stream timeout", response_body);
  EXPECT_EQ(1U, stats_.named_.downstream_rq_idle_timeout_.value());
}

// Test timeout variants.
TEST_F(HttpConnectionManagerImplTest, DurationTimeout) {
  stream_idle_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");
  setupFilterChain(1, 0);
  RequestHeaderMap* latched_headers = nullptr;

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));

  // Create the stream.
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        Event::MockTimer* idle_timer = setUpTimer();
        EXPECT_CALL(*idle_timer, enableTimer(_, _));
        RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
        EXPECT_CALL(*idle_timer, enableTimer(_, _));
        EXPECT_CALL(*idle_timer, disableTimer());
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
        latched_headers = headers.get();
        decoder->decodeHeaders(std::move(headers), false);

        data.drain(4);
        return Http::okStatus();
      }));
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Clear and refresh the route cache (checking clusterInfo refreshes the route cache)
  decoder_filters_[0]->callbacks_->clearRouteCache();
  decoder_filters_[0]->callbacks_->clusterInfo();

  Event::MockTimer* timer = setUpTimer();

  // Set a max duration of 30ms and make sure a 30ms timer is set.
  {
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(30), _));
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, maxStreamDuration())
        .Times(2)
        .WillRepeatedly(Return(std::chrono::milliseconds(30)));
    decoder_filters_[0]->callbacks_->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // Clear the timeout and make sure the timer is disabled.
  {
    EXPECT_CALL(*timer, disableTimer());
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, maxStreamDuration())
        .Times(1)
        .WillRepeatedly(Return(absl::nullopt));
    decoder_filters_[0]->callbacks_->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // With no route timeout, but HCM defaults, the HCM defaults will be used.
  {
    max_stream_duration_ = std::chrono::milliseconds(17);
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(17), _));
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, maxStreamDuration())
        .Times(1)
        .WillRepeatedly(Return(absl::nullopt));
    decoder_filters_[0]->callbacks_->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
    max_stream_duration_ = absl::nullopt;
  }

  // Add a gRPC header, but not a gRPC timeout and verify the timer is unchanged.
  latched_headers->setGrpcTimeout("1M");
  {
    EXPECT_CALL(*timer, disableTimer());
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, maxStreamDuration())
        .Times(1)
        .WillRepeatedly(Return(absl::nullopt));
    decoder_filters_[0]->callbacks_->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // With a gRPC header of 1M and a gRPC header max of 0, respect the gRPC header.
  {
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, grpcTimeoutHeaderMax())
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::chrono::milliseconds(0)));
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(60000), _));
    decoder_filters_[0]->callbacks_->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // With a gRPC header and a larger gRPC header cap, respect the gRPC header.
  {
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, grpcTimeoutHeaderMax())
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::chrono::milliseconds(20000000)));
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(60000), _));
    decoder_filters_[0]->callbacks_->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // With a gRPC header and a small gRPC header cap, use the cap.
  {
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, grpcTimeoutHeaderMax())
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::chrono::milliseconds(20)));
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(20), _));
    decoder_filters_[0]->callbacks_->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  latched_headers->setGrpcTimeout("0m");
  // With a gRPC header of 0, use the header
  {
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, grpcTimeoutHeaderMax())
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::chrono::milliseconds(20)));
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(0), _));
    decoder_filters_[0]->callbacks_->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  latched_headers->setGrpcTimeout("1M");
  // With a timeout of 20ms and an offset of 10ms, set a timeout for 10ms.
  {
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, grpcTimeoutHeaderMax())
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::chrono::milliseconds(20)));
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_,
                grpcTimeoutHeaderOffset())
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::chrono::milliseconds(10)));
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(10), _));
    decoder_filters_[0]->callbacks_->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // With a timeout of 20ms and an offset of 30ms, set a timeout for 0ms
  {
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, grpcTimeoutHeaderMax())
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::chrono::milliseconds(20)));
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_,
                grpcTimeoutHeaderOffset())
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::chrono::milliseconds(30)));
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(0), _));
    decoder_filters_[0]->callbacks_->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // With a gRPC timeout of 20ms, and 5ms used already when the route was
  // refreshed, set a timer for 15ms.
  {
    test_time_.timeSystem().setMonotonicTime(MonotonicTime(std::chrono::milliseconds(5)));
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, grpcTimeoutHeaderMax())
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::chrono::milliseconds(20)));
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_,
                grpcTimeoutHeaderOffset())
        .Times(AnyNumber())
        .WillRepeatedly(Return(absl::nullopt));
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(15), _));
    decoder_filters_[0]->callbacks_->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // With a gRPC timeout of 20ms, and 25ms used already when the route was
  // refreshed, set a timer for now (0ms)
  {
    test_time_.timeSystem().setMonotonicTime(MonotonicTime(std::chrono::milliseconds(25)));
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, grpcTimeoutHeaderMax())
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::chrono::milliseconds(20)));
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_,
                grpcTimeoutHeaderOffset())
        .Times(AnyNumber())
        .WillRepeatedly(Return(absl::nullopt));
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(0), _));
    decoder_filters_[0]->callbacks_->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // Cleanup.
  EXPECT_CALL(*timer, disableTimer());
  EXPECT_CALL(*decoder_filters_[0], onStreamComplete());
  EXPECT_CALL(*decoder_filters_[0], onDestroy());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Per-route timeouts override the global stream idle timeout.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutRouteOverride) {
  stream_idle_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(30)));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        Event::MockTimer* idle_timer = setUpTimer();
        EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
        EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(30), _));
        EXPECT_CALL(*idle_timer, disableTimer());
        decoder_->decodeHeaders(std::move(headers), false);

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_idle_timeout_.value());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Per-route zero timeout overrides the global stream idle timeout.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutRouteZeroOverride) {
  stream_idle_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(0)));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        Event::MockTimer* idle_timer = setUpTimer();
        EXPECT_CALL(*idle_timer, enableTimer(std::chrono::milliseconds(10), _));
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
        EXPECT_CALL(*idle_timer, disableTimer());
        decoder_->decodeHeaders(std::move(headers), false);

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_idle_timeout_.value());

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Validate the per-stream idle timeout after having sent downstream headers.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutAfterDownstreamHeaders) {
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(10)));

  // Codec sends downstream request headers.
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);

    Event::MockTimer* idle_timer = setUpTimer();
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder_->decodeHeaders(std::move(headers), false);

    // Expect resetIdleTimer() to be called for the response
    // encodeHeaders()/encodeData().
    EXPECT_CALL(*idle_timer, enableTimer(_, _)).Times(2);
    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();

    data.drain(4);
    return Http::okStatus();
  }));

  // 408 direct response after timeout.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("408", headers.getStatusValue());
      }));
  std::string response_body;
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ("stream timeout", response_body);
  EXPECT_EQ(1U, stats_.named_.downstream_rq_idle_timeout_.value());
}

// Validate the per-stream idle timer is properly disabled when the stream terminates normally.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutNormalTermination) {
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(10)));

  // Codec sends downstream request headers.
  Event::MockTimer* idle_timer = setUpTimer();
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);

    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder_->decodeHeaders(std::move(headers), false);

    data.drain(4);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*idle_timer, disableTimer());
  conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_idle_timeout_.value());
}

// Validate the per-stream idle timeout after having sent downstream
// headers+body.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutAfterDownstreamHeadersAndBody) {
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(10)));

  // Codec sends downstream request headers.
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);

    Event::MockTimer* idle_timer = setUpTimer();
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder_->decodeHeaders(std::move(headers), false);

    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder_->decodeData(data, false);

    // Expect resetIdleTimer() to be called for the response
    // encodeHeaders()/encodeData().
    EXPECT_CALL(*idle_timer, enableTimer(_, _)).Times(2);
    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();

    data.drain(4);
    return Http::okStatus();
  }));

  // 408 direct response after timeout.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("408", headers.getStatusValue());
      }));
  std::string response_body;
  EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ("stream timeout", response_body);
  EXPECT_EQ(1U, stats_.named_.downstream_rq_idle_timeout_.value());
}

// Validate the per-stream idle timeout after upstream headers have been sent.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutAfterUpstreamHeaders) {
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(10)));

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  // Codec sends downstream request headers, upstream response headers are
  // encoded.
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);

    Event::MockTimer* idle_timer = setUpTimer();
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder_->decodeHeaders(std::move(headers), false);

    ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    filter->callbacks_->encodeHeaders(std::move(response_headers), false, "details");

    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();

    data.drain(4);
    return Http::okStatus();
  }));

  // 200 upstream response.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("200", headers.getStatusValue());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_idle_timeout_.value());
}

// Validate the per-stream idle timeout after a sequence of header/data events.
TEST_F(HttpConnectionManagerImplTest, PerStreamIdleTimeoutAfterBidiData) {
  setup(false, "");
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(10)));
  proxy_100_continue_ = true;

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  // Codec sends downstream request headers, upstream response headers are
  // encoded, data events happen in various directions.
  Event::MockTimer* idle_timer = setUpTimer();
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder_->decodeHeaders(std::move(headers), false);

    ResponseHeaderMapPtr response_continue_headers{
        new TestResponseHeaderMapImpl{{":status", "100"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    filter->callbacks_->encode100ContinueHeaders(std::move(response_continue_headers));

    ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    filter->callbacks_->streamInfo().setResponseCodeDetails("");
    filter->callbacks_->encodeHeaders(std::move(response_headers), false, "details");

    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder_->decodeData(data, false);

    RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    decoder_->decodeTrailers(std::move(trailers));

    Buffer::OwnedImpl fake_response("world");
    EXPECT_CALL(*idle_timer, enableTimer(_, _));
    filter->callbacks_->encodeData(fake_response, false);

    EXPECT_CALL(*idle_timer, disableTimer());
    idle_timer->invokeCallback();

    data.drain(4);
    return Http::okStatus();
  }));

  // 100 continue.
  EXPECT_CALL(response_encoder_, encode100ContinueHeaders(_));

  // 200 upstream response.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("200", headers.getStatusValue());
      }));

  std::string response_body;
  EXPECT_CALL(response_encoder_, encodeData(_, false)).WillOnce(AddBufferToString(&response_body));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_idle_timeout_.value());
  EXPECT_EQ("world", response_body);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutDisabledByDefault) {
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, createTimer_).Times(0);
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutDisabledIfSetToZero) {
  request_timeout_ = std::chrono::milliseconds(0);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, createTimer_).Times(0);
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutValidlyConfigured) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _));
    EXPECT_CALL(*request_timer, disableTimer());

    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutCallbackDisarmsAndReturns408) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  std::string response_body;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);
    EXPECT_CALL(*request_timer, disableTimer()).Times(AtLeast(1));

    EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
        .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
          EXPECT_EQ("408", headers.getStatusValue());
        }));
    EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

    conn_manager_->newStream(response_encoder_);
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, setTrackedObject(_)).Times(2);
    request_timer->invokeCallback();
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_EQ(1U, stats_.named_.downstream_rq_timeout_.value());
  EXPECT_EQ("request timeout", response_body);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsNotDisarmedOnIncompleteRequestWithHeader) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);
    EXPECT_CALL(*request_timer, disableTimer()).Times(1);

    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

    // the second parameter 'false' leaves the stream open
    decoder_->decodeHeaders(std::move(headers), false);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsDisarmedOnCompleteRequestWithHeader) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);

    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

    EXPECT_CALL(*request_timer, disableTimer()).Times(2);
    decoder_->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsDisarmedOnCompleteRequestWithData) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);

    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
    decoder_->decodeHeaders(std::move(headers), false);

    EXPECT_CALL(*request_timer, disableTimer()).Times(2);
    decoder_->decodeData(data, true);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsDisarmedOnCompleteRequestWithTrailers) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);
    decoder_ = &conn_manager_->newStream(response_encoder_);

    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), false);
    decoder_->decodeData(data, false);

    EXPECT_CALL(*request_timer, disableTimer()).Times(2);
    RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
    decoder_->decodeTrailers(std::move(trailers));
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsDisarmedOnEncodeHeaders) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, _));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);

    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

    decoder_->decodeHeaders(std::move(headers), false);

    EXPECT_CALL(*request_timer, disableTimer()).Times(2);
    ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
    filter->callbacks_->streamInfo().setResponseCodeDetails("");
    filter->callbacks_->encodeHeaders(std::move(response_headers), false, "details");
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());
  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestTimeoutIsDisarmedOnConnectionTermination) {
  request_timeout_ = std::chrono::milliseconds(10);
  setup(false, "");

  Event::MockTimer* request_timer = setUpTimer();
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

    decoder_->decodeHeaders(std::move(headers), false);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");

  EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _)).Times(1);
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_CALL(*request_timer, disableTimer()).Times(1);
  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, MaxStreamDurationDisabledIfSetToZero) {
  max_stream_duration_ = std::chrono::milliseconds(0);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, createTimer_).Times(0);
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, MaxStreamDurationValidlyConfigured) {
  max_stream_duration_ = std::chrono::milliseconds(10);
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* duration_timer = setUpTimer();

    EXPECT_CALL(*duration_timer, enableTimer(max_stream_duration_.value(), _));
    EXPECT_CALL(*duration_timer, disableTimer());
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, MaxStreamDurationCallbackResetStream) {
  max_stream_duration_ = std::chrono::milliseconds(10);
  setup(false, "");
  Event::MockTimer* duration_timer = setUpTimer();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    EXPECT_CALL(*duration_timer, enableTimer(max_stream_duration_.value(), _)).Times(1);
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_CALL(*duration_timer, disableTimer());
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  EXPECT_CALL(response_encoder_, encodeData(_, true));
  duration_timer->invokeCallback();

  EXPECT_EQ(1U, stats_.named_.downstream_rq_max_duration_reached_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_rx_reset_.value());
}

TEST_F(HttpConnectionManagerImplTest, Http10Rejected) {
  setup(false, "");
  EXPECT_CALL(*codec_, protocol()).Times(AnyNumber()).WillRepeatedly(Return(Protocol::Http10));
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":method", "GET"}, {":path", "/"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("426", headers.getStatusValue());
        EXPECT_EQ("close", headers.getConnectionValue());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, Http10ConnCloseLegacy) {
  http1_settings_.accept_http_10_ = true;
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.fixed_connection_close", "false"}});
  setup(false, "");
  EXPECT_CALL(*codec_, protocol()).Times(AnyNumber()).WillRepeatedly(Return(Protocol::Http10));
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host:80"}, {":method", "CONNECT"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("close", headers.getConnectionValue());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, ProxyConnectLegacyClose) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.fixed_connection_close", "false"}});
  setup(false, "");
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host:80"}, {":method", "CONNECT"}, {"proxy-connection", "close"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("close", headers.getConnectionValue());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, ConnectLegacyClose) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.fixed_connection_close", "false"}});
  setup(false, "");
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "host"}, {":method", "CONNECT"}, {"connection", "close"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("close", headers.getConnectionValue());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, MaxStreamDurationCallbackNotCalledIfResetStreamValidly) {
  max_stream_duration_ = std::chrono::milliseconds(5000);
  setup(false, "");
  Event::MockTimer* duration_timer = setUpTimer();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    EXPECT_CALL(*duration_timer, enableTimer(max_stream_duration_.value(), _)).Times(1);
    conn_manager_->newStream(response_encoder_);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_CALL(*duration_timer, disableTimer());
  conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);

  EXPECT_EQ(0U, stats_.named_.downstream_rq_max_duration_reached_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_rx_reset_.value());
}

TEST_F(HttpConnectionManagerImplTest, RejectWebSocketOnNonWebSocketRoute) {
  setup(false, "");
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{{":authority", "host"},
                                                             {":method", "GET"},
                                                             {":path", "/"},
                                                             {"connection", "Upgrade"},
                                                             {"upgrade", "websocket"}}};
    decoder_->decodeHeaders(std::move(headers), false);
    // Try sending trailers after the headers which will be rejected, just to
    // test the HCM logic that further decoding will not be passed to the
    // filters once the early response path is kicked off.
    RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"bazzz", "bar"}}};
    decoder_->decodeTrailers(std::move(trailers));
    data.drain(4);
    return Http::okStatus();
  }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("403", headers.getStatusValue());
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ(1U, stats_.named_.downstream_rq_ws_on_non_ws_route_.value());
}

// Make sure for upgrades, we do not append Connection: Close when draining.
TEST_F(HttpConnectionManagerImplTest, FooUpgradeDrainClose) {
  setup(false, "envoy-custom-server", false);

  // Store the basic request encoder during filter chain setup.
  auto* filter = new MockStreamFilter();
  EXPECT_CALL(drain_close_, drainClose()).WillOnce(Return(true));

  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillRepeatedly(Invoke([&](RequestHeaderMap&, bool) -> FilterHeadersStatus {
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*filter, encodeHeaders(_, false))
      .WillRepeatedly(Invoke(
          [&](HeaderMap&, bool) -> FilterHeadersStatus { return FilterHeadersStatus::Continue; }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_NE(nullptr, headers.Connection());
        EXPECT_EQ("upgrade", headers.getConnectionValue());
      }));

  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));

  EXPECT_CALL(filter_factory_, createUpgradeFilterChain(_, _, _))
      .WillRepeatedly(Invoke([&](absl::string_view, const Http::FilterChainFactory::UpgradeMap*,
                                 FilterChainFactoryCallbacks& callbacks) -> bool {
        callbacks.addStreamFilter(StreamFilterSharedPtr{filter});
        return true;
      }));

  // When dispatch is called on the codec, we pretend to get a new stream and then fire a headers
  // only request into it. Then we respond into the filter.
  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{{":authority", "host"},
                                                                 {":method", "GET"},
                                                                 {":path", "/"},
                                                                 {"connection", "Upgrade"},
                                                                 {"upgrade", "foo"}}};
        decoder_->decodeHeaders(std::move(headers), false);

        filter->decoder_callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{
            {":status", "101"}, {"Connection", "upgrade"}, {"upgrade", "foo"}}};
        filter->decoder_callbacks_->encodeHeaders(std::move(response_headers), false, "details");

        data.drain(4);
        return Http::okStatus();
      }));

  // Kick off the incoming data. Use extra data which should cause a redispatch.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*filter, onStreamComplete());
  EXPECT_CALL(*filter, onDestroy());
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Make sure CONNECT requests hit the upgrade filter path.
TEST_F(HttpConnectionManagerImplTest, ConnectAsUpgrade) {
  setup(false, "envoy-custom-server", false);

  EXPECT_CALL(filter_factory_, createUpgradeFilterChain("CONNECT", _, _))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);
        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":authority", "host"}, {":method", "CONNECT"}}};
        decoder_->decodeHeaders(std::move(headers), false);
        data.drain(4);
        return Http::okStatus();
      }));

  // Kick off the incoming data. Use extra data which should cause a redispatch.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, ConnectWithEmptyPath) {
  setup(false, "envoy-custom-server", false);

  EXPECT_CALL(filter_factory_, createUpgradeFilterChain("CONNECT", _, _))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", ""}, {":method", "CONNECT"}}};
        decoder_->decodeHeaders(std::move(headers), false);
        data.drain(4);
        return Http::okStatus();
      }));

  // Kick off the incoming data. Use extra data which should cause a redispatch.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  expectOnDestroy(false);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, ConnectLegacy) {
  TestScopedRuntime scoped_runtime;
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"envoy.reloadable_features.stop_faking_paths", "false"}});

  setup(false, "envoy-custom-server", false);

  EXPECT_CALL(filter_factory_, createUpgradeFilterChain("CONNECT", _, _))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);
        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":authority", "host"}, {":method", "CONNECT"}}};
        decoder_->decodeHeaders(std::move(headers), false);
        data.drain(4);
        return Http::okStatus();
      }));

  EXPECT_CALL(response_encoder_, encodeHeaders(_, _))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("403", headers.getStatusValue());
      }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Regression test for https://github.com/envoyproxy/envoy/issues/10138
TEST_F(HttpConnectionManagerImplTest, DrainCloseRaceWithClose) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(drain_close_, drainClose()).WillOnce(Return(true));
  EXPECT_CALL(*codec_, shutdownNotice());
  Event::MockTimer* drain_timer = setUpTimer();
  EXPECT_CALL(*drain_timer, enableTimer(_, _));
  expectOnDestroy();
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true, "details");

  // Fake a protocol error that races with the drain timeout. This will cause a local close.
  // Also fake the local close not closing immediately.
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Return(codecProtocolError("protocol error")));
  EXPECT_CALL(*drain_timer, disableTimer());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay))
      .WillOnce(Return());
  conn_manager_->onData(fake_input, false);

  // Now fire the close event which should have no effect as all close work has already been done.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(HttpConnectionManagerImplTest,
       FilterThatWaitsForBodyCanBeCalledAfterFilterThatAddsBodyEvenIfItIsNotLast) {
  InSequence s;
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  // 3 filters:
  // 1st filter adds a body
  // 2nd filter waits for the body
  // 3rd filter simulates router filter.
  setupFilterChain(3, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(Invoke([&](RequestHeaderMap&, bool) -> FilterHeadersStatus {
        Buffer::OwnedImpl body("body");
        decoder_filters_[0]->callbacks_->addDecodedData(body, false);
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Invoke([](RequestHeaderMap&, bool) -> FilterHeadersStatus {
        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true))
      .WillOnce(Invoke(
          [](Buffer::Instance&, bool) -> FilterDataStatus { return FilterDataStatus::Continue; }));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());
  EXPECT_CALL(*decoder_filters_[2], decodeHeaders(_, false))
      .WillOnce(Invoke([](RequestHeaderMap&, bool) -> FilterHeadersStatus {
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[2], decodeData(_, true))
      .WillOnce(Invoke(
          [](Buffer::Instance&, bool) -> FilterDataStatus { return FilterDataStatus::Continue; }));
  EXPECT_CALL(*decoder_filters_[2], decodeComplete());

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, DrainClose) {
  setup(true, "");

  MockStreamDecoderFilter* filter = new NiceMock<MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(StreamDecoderFilterSharedPtr{filter});
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_NE(nullptr, headers.ForwardedFor());
        EXPECT_EQ("https", headers.getForwardedProtoValue());
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), true);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "300"}}};
  Event::MockTimer* drain_timer = setUpTimer();
  EXPECT_CALL(*drain_timer, enableTimer(_, _));
  EXPECT_CALL(drain_close_, drainClose()).WillOnce(Return(true));
  EXPECT_CALL(*codec_, shutdownNotice());
  filter->callbacks_->streamInfo().setResponseCodeDetails("");
  filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
  EXPECT_EQ(ssl_connection_.get(), filter->callbacks_->connection()->ssl().get());

  EXPECT_CALL(*codec_, goAway());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay));
  EXPECT_CALL(*drain_timer, disableTimer());
  drain_timer->invokeCallback();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_drain_close_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_3xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_3xx_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_completed_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_completed_.value());
}

} // namespace Http
} // namespace Envoy
