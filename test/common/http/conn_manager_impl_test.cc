#include <chrono>

#include "test/common/http/conn_manager_impl_test_base.h"
#include "test/mocks/http/early_header_mutation.h"
#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"

#include "conn_manager_impl_test_base.h"

using testing::_;
using testing::An;
using testing::AnyNumber;
using testing::AtLeast;
using testing::Eq;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Mock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Http {

TEST_F(HttpConnectionManagerImplTest, HeaderOnlyRequestAndResponse) {
  setup(SetupOpts().setTracing(false));

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
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
        response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();

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

// Similar to HeaderOnlyRequestAndResponse but uses newStreamHandle and has
// lifetime checks.
TEST_F(HttpConnectionManagerImplTest, HandleLifetime) {
  setup(SetupOpts().setTracing(false));
  Http::RequestDecoderHandlePtr decoder_handle;

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
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(2);

  // When dispatch is called on the codec, we pretend to get a new stream and then fire a headers
  // only request into it. Then we respond into the filter.
  EXPECT_CALL(*codec_, dispatch(_))
      .Times(2)
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_handle = conn_manager_->newStreamHandle(response_encoder_);
        EXPECT_TRUE(decoder_handle->get().has_value());
        decoder_ = &decoder_handle->get().value().get();

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
        response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();

        // Drain 2 so that on the 2nd iteration we will hit zero.
        data.drain(2);
        return Http::okStatus();
      }));

  // Kick off the incoming data. Use extra data which should cause a redispatch.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // At the end of this, the handle is on the deferred delete list but not
  // deleted. It should be invalid.
  EXPECT_FALSE(decoder_handle->get().has_value());
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_FALSE(decoder_handle->get().has_value());

  EXPECT_EQ(1U, stats_.named_.downstream_rq_2xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_2xx_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_completed_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_completed_.value());
}

TEST_F(HttpConnectionManagerImplTest, HeaderOnlyRequestAndResponseWithEarlyHeaderMutation) {
  setup(SetupOpts().setTracing(false));

  auto mock_early_header_mutation_1 = std::make_unique<NiceMock<Http::MockEarlyHeaderMutation>>();
  auto mock_early_header_mutation_2 = std::make_unique<NiceMock<Http::MockEarlyHeaderMutation>>();
  auto raw_mock_early_header_mutation_1 = mock_early_header_mutation_1.get();
  auto raw_mock_early_header_mutation_2 = mock_early_header_mutation_2.get();

  // Set early header mutations.
  early_header_mutations_.push_back(std::move(mock_early_header_mutation_1));
  early_header_mutations_.push_back(std::move(mock_early_header_mutation_2));

  EXPECT_CALL(*raw_mock_early_header_mutation_1, mutate(_, _))
      .Times(2)
      .WillRepeatedly(Invoke([](RequestHeaderMap& headers, const StreamInfo::StreamInfo&) -> bool {
        headers.addCopy(Http::LowerCaseString("x-early-mutation"), "true");
        return false;
      }));
  // This should not be called because the first extension returns false.
  EXPECT_CALL(*raw_mock_early_header_mutation_2, mutate(_, _)).Times(0);

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .Times(2)
      .WillRepeatedly(Invoke([&](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        EXPECT_EQ(
            "true",
            headers.get(Http::LowerCaseString("x-early-mutation"))[0]->value().getStringView());

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
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
        response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();

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

TEST_F(HttpConnectionManagerImplTest, 1xxResponse) {
  proxy_100_continue_ = true;
  setup(SetupOpts().setTracing(false));

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
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
        filter->callbacks_->encode1xxHeaders(std::move(continue_headers));
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

TEST_F(HttpConnectionManagerImplTest, 1xxResponseWithEncoderFiltersProxyingDisabled) {
  proxy_100_continue_ = false;
  setup(SetupOpts().setTracing(false));
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Akin to 1xxResponseWithEncoderFilters below, but with
  // proxy_100_continue_ false. Verify the filters do not get the 100 continue
  // headers.
  EXPECT_CALL(*encoder_filters_[0], encode1xxHeaders(_)).Times(0);
  EXPECT_CALL(*encoder_filters_[1], encode1xxHeaders(_)).Times(0);
  EXPECT_CALL(response_encoder_, encode1xxHeaders(_)).Times(0);
  ResponseHeaderMapPtr continue_headers{new TestResponseHeaderMapImpl{{":status", "100"}}};
  decoder_filters_[0]->callbacks_->encode1xxHeaders(std::move(continue_headers));

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

TEST_F(HttpConnectionManagerImplTest, 1xxResponseWithEncoderFilters) {
  proxy_100_continue_ = true;
  setup(SetupOpts().setTracing(false));
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  EXPECT_CALL(*encoder_filters_[0], encode1xxHeaders(_))
      .WillOnce(Return(Filter1xxHeadersStatus::Continue));
  EXPECT_CALL(*encoder_filters_[1], encode1xxHeaders(_))
      .WillOnce(Return(Filter1xxHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encode1xxHeaders(_));
  ResponseHeaderMapPtr continue_headers{new TestResponseHeaderMapImpl{{":status", "100"}}};
  decoder_filters_[0]->callbacks_->encode1xxHeaders(std::move(continue_headers));

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

TEST_F(HttpConnectionManagerImplTest, PauseResume1xx) {
  proxy_100_continue_ = true;
  setup(SetupOpts().setTracing(false));
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();

  // Stop the 100-Continue at encoder filter 1. Encoder filter 0 should not yet receive the
  // 100-Continue
  EXPECT_CALL(*encoder_filters_[1], encode1xxHeaders(_))
      .WillOnce(Return(Filter1xxHeadersStatus::StopIteration));
  EXPECT_CALL(*encoder_filters_[0], encode1xxHeaders(_)).Times(0);
  EXPECT_CALL(response_encoder_, encode1xxHeaders(_)).Times(0);
  ResponseHeaderMapPtr continue_headers{new TestResponseHeaderMapImpl{{":status", "100"}}};
  decoder_filters_[1]->callbacks_->encode1xxHeaders(std::move(continue_headers));

  // Have the encoder filter 1 continue. Make sure the 100-Continue is resumed as expected.
  EXPECT_CALL(*encoder_filters_[0], encode1xxHeaders(_))
      .WillOnce(Return(Filter1xxHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encode1xxHeaders(_));
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
TEST_F(HttpConnectionManagerImplTest, 1xxResponseWithDecoderPause) {
  proxy_100_continue_ = true;
  setup(SetupOpts().setTracing(false));

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
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
        filter->callbacks_->encode1xxHeaders(std::move(continue_headers));

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

// When create new stream, the stream info will be populated from the connection.
TEST_F(HttpConnectionManagerImplTest, PopulateStreamInfo) {
  setup(SetupOpts().setSsl(true).setTracing(false));

  // Set up the codec.
  Buffer::OwnedImpl fake_input("input");
  conn_manager_->createCodec(fake_input);

  startRequest(false);

  EXPECT_NE(absl::nullopt, decoder_->streamInfo().getStreamIdProvider());
  EXPECT_NE(absl::nullopt, decoder_->streamInfo().getStreamIdProvider()->toInteger());
  EXPECT_NE(absl::nullopt, decoder_->streamInfo().getStreamIdProvider()->toStringView());
  EXPECT_EQ(ssl_connection_, decoder_->streamInfo().downstreamAddressProvider().sslConnection());
  EXPECT_EQ(filter_callbacks_.connection_.id_,
            decoder_->streamInfo().downstreamAddressProvider().connectionID().value());
  EXPECT_EQ(server_name_, decoder_->streamInfo().downstreamAddressProvider().requestedServerName());
  EXPECT_TRUE(decoder_->streamInfo().filterState()->hasDataWithName(
      Network::ProxyProtocolFilterState::key()));

  // Clean up.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// By default, Envoy will set the server header to the server name, here "custom-value"
TEST_F(HttpConnectionManagerImplTest, ServerHeaderOverwritten) {
  setup(SetupOpts().setServerName("custom-value").setTracing(false));
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
  setup(SetupOpts().setServerName("custom-value").setTracing(false));
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
  setup(SetupOpts().setServerName("custom-value").setTracing(false));
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
  setup(SetupOpts().setServerName("custom-value").setTracing(false));
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
  setup(SetupOpts().setServerName("custom-value").setTracing(false));
  setUpEncoderAndDecoder(false, false);

  sendRequestHeadersAndData();
  const ResponseHeaderMap* altered_headers =
      sendResponseHeaders(ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}});
  EXPECT_TRUE(altered_headers->Server() == nullptr);

  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, InvalidPathWithDualFilter) {
  InSequence s;
  setup();

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
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createStreamFilterFactoryCb(StreamFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));
  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));

  EXPECT_CALL(*filter, encodeHeaders(_, true));
  EXPECT_CALL(*filter, encodeComplete());
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
  setup();
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
#ifdef ENVOY_ENABLE_UHV
  expectCheckWithDefaultUhv();
#endif
  EXPECT_CALL(response_encoder_, streamErrorOnInvalidHttpMessage()).WillRepeatedly(Return(true));

  // This test also verifies that decoder/encoder filters have onDestroy() called only once.
  auto* filter = new MockStreamFilter();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createStreamFilterFactoryCb(StreamFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));
  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter, encodeHeaders(_, true));
  EXPECT_CALL(*filter, encodeComplete());
  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("400", headers.getStatusValue());
        // Error details are different in UHV and legacy modes
        EXPECT_THAT(filter->decoder_callbacks_->streamInfo().responseCodeDetails().value(),
                    HasSubstr("path"));
      }));
  EXPECT_CALL(*filter, onStreamComplete());
  EXPECT_CALL(*filter, onDestroy());

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

// Filters observe normalized paths, not the original path, when path
// normalization is configured.
TEST_F(HttpConnectionManagerImplTest, FilterShouldUseSantizedPath) {
  setup();
  // Enable path sanitizer
  normalize_path_ = true;
  const std::string original_path = "/x/%2E%2e/z";
  const std::string normalized_path = "/z";
#ifdef ENVOY_ENABLE_UHV
  expectCheckWithDefaultUhv();
#endif

  auto* filter = new MockStreamFilter();

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));

  EXPECT_CALL(*filter, decodeComplete());
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
  setup();
  // Enable path sanitizer
  normalize_path_ = true;
  const std::string original_path = "/x/%2E%2e/z";
  const std::string normalized_path = "/z";
#ifdef ENVOY_ENABLE_UHV
  expectCheckWithDefaultUhv();
#endif

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
      .WillOnce(Invoke([&](FilterChainManager&) -> bool { return false; }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Clean up.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Paths with escaped slashes rejected with 400 when configured.
TEST_F(HttpConnectionManagerImplTest, PathWithEscapedSlashesRejected) {
  path_with_escaped_slashes_action_ = envoy::extensions::filters::network::http_connection_manager::
      v3::HttpConnectionManager::REJECT_REQUEST;
  testPathNormalization(
      TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/abc%5c../"}, {":method", "GET"}},
      TestResponseHeaderMapImpl{{":status", "400"}, {"connection", "close"}});
  EXPECT_EQ(1U, stats_.named_.downstream_rq_failed_path_normalization_.value());
}

// Paths with escaped slashes redirected when configured.
TEST_F(HttpConnectionManagerImplTest, PathWithEscapedSlashesRedirected) {
  path_with_escaped_slashes_action_ = envoy::extensions::filters::network::http_connection_manager::
      v3::HttpConnectionManager::UNESCAPE_AND_REDIRECT;
  testPathNormalization(
      TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/abc%2f../"}, {":method", "GET"}},
      TestResponseHeaderMapImpl{{":status", "307"}, {"location", "/abc/../"}});
  EXPECT_EQ(1U, stats_.named_.downstream_rq_redirected_with_normalized_path_.value());
}

// Paths with escaped slashes rejected with 400 instead of redirected for gRPC request.
TEST_F(HttpConnectionManagerImplTest, PathWithEscapedSlashesRejectedIfGRPC) {
  // This test is slightly weird as it sends gRPC "request" over H/1 client of the
  // HttpConnectionManagerImplTest. However it is sufficient to test the behavior of path
  // normalization as it is determined by the content type only.
  path_with_escaped_slashes_action_ = envoy::extensions::filters::network::http_connection_manager::
      v3::HttpConnectionManager::UNESCAPE_AND_REDIRECT;
  testPathNormalization(TestRequestHeaderMapImpl{{":authority", "host"},
                                                 {":path", "/abc%2fdef"},
                                                 {":method", "GET"},
                                                 {"content-type", "application/grpc"}},
                        TestResponseHeaderMapImpl{{":status", "200"},
                                                  {"connection", "close"},
                                                  {"grpc-status", "13"},
                                                  {"content-type", "application/grpc"}});
  EXPECT_EQ(1U, stats_.named_.downstream_rq_failed_path_normalization_.value());
}

// Test that requests with escaped slashes are redirected when configured. Redirection
// occurs after Chromium URL normalization or merge slashes operations.
TEST_F(HttpConnectionManagerImplTest, EscapedSlashesRedirectedAfterOtherNormalizations) {
  normalize_path_ = true;
  merge_slashes_ = true;
  path_with_escaped_slashes_action_ = envoy::extensions::filters::network::http_connection_manager::
      v3::HttpConnectionManager::UNESCAPE_AND_REDIRECT;
  // Both Chromium URL normalization and merge slashes should happen if request is redirected
  // due to escaped slash sequences.
  testPathNormalization(TestRequestHeaderMapImpl{{":authority", "host"},
                                                 {":path", "/abc%2f../%5cdef//"},
                                                 {":method", "GET"}},
                        TestResponseHeaderMapImpl{{":status", "307"}, {"location", "/def/"}});
  EXPECT_EQ(1U, stats_.named_.downstream_rq_redirected_with_normalized_path_.value());
}

TEST_F(HttpConnectionManagerImplTest, AllNormalizationsWithEscapedSlashesForwarded) {
  setup();
  // Enable path sanitizer
  normalize_path_ = true;
  merge_slashes_ = true;
  path_with_escaped_slashes_action_ = envoy::extensions::filters::network::http_connection_manager::
      v3::HttpConnectionManager::UNESCAPE_AND_FORWARD;
  const std::string original_path = "/x/%2E%2e/z%2f%2Fabc%5C../def";
  const std::string normalized_path = "/z/def";

#ifdef ENVOY_ENABLE_UHV
  expectCheckWithDefaultUhv();
#endif

  auto* filter = new MockStreamFilter();

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));

  EXPECT_CALL(*filter, decodeComplete());
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

TEST_F(HttpConnectionManagerImplTest, RouteOverride) {
  setup();

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
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view{foo_bar_cluster_name}))
      .WillOnce(Return(foo_bar_cluster.get()));

  std::shared_ptr<Upstream::MockThreadLocalCluster> foo_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();

  std::shared_ptr<Upstream::MockThreadLocalCluster> default_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view{default_cluster_name}))
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
          EXPECT_EQ(default_route, decoder_filters_[0]->callbacks_->streamInfo().route());
          EXPECT_EQ(default_cluster->info(), decoder_filters_[0]->callbacks_->clusterInfo());

          // Not clearing cached route returns cached route and doesn't invoke cb.
          Router::RouteConstSharedPtr route =
              decoder_filters_[0]->callbacks_->downstreamCallbacks()->route(
                  [](Router::RouteConstSharedPtr,
                     Router::RouteEvalStatus) -> Router::RouteMatchStatus {
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

          decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
          route = decoder_filters_[0]->callbacks_->downstreamCallbacks()->route(cb);

          EXPECT_EQ(default_route, route);
          EXPECT_EQ(default_route, decoder_filters_[0]->callbacks_->route());
          EXPECT_EQ(default_route, decoder_filters_[0]->callbacks_->streamInfo().route());
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
          EXPECT_EQ(default_route, decoder_filters_[1]->callbacks_->streamInfo().route());
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

          decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
          decoder_filters_[1]->callbacks_->downstreamCallbacks()->route(cb);

          EXPECT_EQ(foo_bar_route, decoder_filters_[1]->callbacks_->route());
          EXPECT_EQ(foo_bar_route, decoder_filters_[1]->callbacks_->streamInfo().route());
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

// The router observes the cached route as a DelegatingRoute (with upstream cluster foo), not the
// original route returned by route config (upstream cluster default), when the setRoute filter
// callback is applied.
TEST_F(HttpConnectionManagerImplTest, FilterSetRouteToDelegatingRouteWithClusterOverride) {
  setup();
  setupFilterChain(2, 0);

  // Cluster mocks: default and foo
  const std::string default_cluster_name = "default";
  const std::string foo_cluster_name = "foo";

  std::shared_ptr<Upstream::MockThreadLocalCluster> default_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view{default_cluster_name}))
      .Times(1)
      .WillRepeatedly(Return(default_cluster.get()));

  std::shared_ptr<Upstream::MockThreadLocalCluster> foo_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view{foo_cluster_name}))
      .Times(1)
      .WillRepeatedly(Return(foo_cluster.get()));

  // Route mock: default
  std::shared_ptr<Router::MockRoute> default_route =
      std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(default_route->route_entry_, clusterName())
      .Times(2) // Expected call twice: once from refreshCachedRoute, once from EXPECT_EQ macro
      .WillRepeatedly(ReturnRef(default_cluster_name));

  // RouteConstSharedPtr of DelegatingRoute for foo
  // Initialization separate from declaration to be in scope for both decoder_filters_
  std::shared_ptr<const Router::ExampleDerivedDelegatingRoute> foo_route_override(nullptr);

  // Route config mock
  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
      .WillOnce(Return(default_route));

  // Filter that performs setRoute (sets cached_route_ & cached_cluster_info_)
  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        // Calls ConnectionManagerImpl::ActiveStream::route(cb), which calls
        // refreshCachedRoute(cb), which (1) calls route_config_->route(_, _, _, _) mock to set
        // default_route as cached_route_, and (2) calls getThreadLocalCluster mock to set
        // cached_cluster_info_.
        EXPECT_EQ(default_route, decoder_filters_[0]->callbacks_->route());
        EXPECT_EQ(default_cluster_name,
                  decoder_filters_[0]->callbacks_->route()->routeEntry()->clusterName());
        EXPECT_EQ(default_route, decoder_filters_[0]->callbacks_->streamInfo().route());
        EXPECT_EQ(default_cluster->info(), decoder_filters_[0]->callbacks_->clusterInfo());

        // Instantiate a DelegatingRoute child class object and invoke setRoute from
        // StreamFilterCallbacks to manually override the cached route for the current request.
        foo_route_override = std::make_shared<Router::ExampleDerivedDelegatingRoute>(
            decoder_filters_[0]->callbacks_->route(), foo_cluster_name);
        decoder_filters_[0]->callbacks_->downstreamCallbacks()->setRoute(foo_route_override);

        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  // Router filter
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        // Returns cached_route, does not invoke route(cb)
        EXPECT_EQ(foo_route_override, decoder_filters_[1]->callbacks_->route());
        // Note: The route filter determines the finalized route's upstream cluster name via
        // routeEntry()->clusterName(), so that's the key piece to check.
        // This should directly call the ExampleDerivedDelegatingRouteEntry overridden
        // clusterName() method.
        EXPECT_EQ(foo_cluster_name,
                  decoder_filters_[1]->callbacks_->route()->routeEntry()->clusterName());
        EXPECT_EQ(foo_route_override, decoder_filters_[1]->callbacks_->streamInfo().route());
        // Tests that setRoute correctly sets cached_cluster_info_
        EXPECT_EQ(foo_cluster->info(), decoder_filters_[1]->callbacks_->clusterInfo());

        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  // Kick off the incoming data. end_stream set to true to indicate this is a header only request.
  startRequest(true);

  // Clean up.
  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Test that all methods supported by DelegatingRouteEntry delegate correctly
TEST_F(HttpConnectionManagerImplTest, DelegatingRouteEntryAllCalls) {
  setup();
  setupFilterChain(2, 0);

  // Cluster mock: foo
  // For when decoder_filters_[0] invokes setRoute
  const std::string foo_cluster_name = "foo";
  std::shared_ptr<Upstream::MockThreadLocalCluster> foo_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view{foo_cluster_name}))
      .Times(1)
      .WillRepeatedly(Return(foo_cluster.get()));

  // Cluster mock: default
  // For decoder_filters_[0] invokes decodeHeaders and subsequently refreshCachedRoute
  const std::string default_cluster_name = "default";
  std::shared_ptr<Upstream::MockThreadLocalCluster> default_cluster =
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>();
  EXPECT_CALL(cluster_manager_, getThreadLocalCluster(absl::string_view{default_cluster_name}))
      .Times(1)
      .WillRepeatedly(Return(default_cluster.get()));

  // Route mock: default
  // For when decoder_filters_[0] invokes decodeHeaders and subsequently refreshCachedRoute
  std::shared_ptr<Router::MockRoute> default_route =
      std::make_shared<NiceMock<Router::MockRoute>>();
  EXPECT_CALL(*route_config_provider_.route_config_, route(_, _, _, _))
      .Times(1)
      .WillRepeatedly(Return(default_route));
  EXPECT_CALL(default_route->route_entry_, clusterName())
      .Times(1)
      .WillRepeatedly(ReturnRef(default_cluster_name));

  // DelegatingRoute: foo
  std::shared_ptr<const Router::ExampleDerivedDelegatingRoute> delegating_route_foo(nullptr);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        // Instantiate a DelegatingRoute child class object and invoke setRoute from
        // StreamFilterCallbacks to manually override the cached route for the current request.
        delegating_route_foo = std::make_shared<Router::ExampleDerivedDelegatingRoute>(
            default_route, foo_cluster_name);
        decoder_filters_[0]->callbacks_->downstreamCallbacks()->setRoute(delegating_route_foo);

        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());

  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterHeadersStatus {
        // Check that cached_route was correctly set to the delegating route.
        EXPECT_EQ(delegating_route_foo, decoder_filters_[1]->callbacks_->route());

        // Check that delegating route correctly overrides the routeEntry()->clusterName()
        EXPECT_EQ(foo_cluster_name, delegating_route_foo->routeEntry()->clusterName());

        // Check that all other routeEntry calls are delegated correctly
        EXPECT_EQ(default_route->routeEntry()->clusterNotFoundResponseCode(),
                  delegating_route_foo->routeEntry()->clusterNotFoundResponseCode());
        EXPECT_EQ(default_route->routeEntry()->corsPolicy(),
                  delegating_route_foo->routeEntry()->corsPolicy());

        auto test_req_headers = Http::TestRequestHeaderMapImpl{{":authority", "www.choice.com"},
                                                               {":path", "/new_endpoint/foo"},
                                                               {":method", "GET"},
                                                               {"x-forwarded-proto", "http"}};
        EXPECT_EQ(default_route->routeEntry()->currentUrlPathAfterRewrite(test_req_headers),
                  delegating_route_foo->routeEntry()->currentUrlPathAfterRewrite(test_req_headers));

        EXPECT_EQ(default_route->routeEntry()->hashPolicy(),
                  delegating_route_foo->routeEntry()->hashPolicy());

        // HedgePolicy objects don't have valid comparison operator, so compare 2 attributes at
        // random. Will apply this strategy for all objects that don't have a valid comparison
        // operator (next example being RetryPolicy).
        EXPECT_EQ(default_route->routeEntry()->hedgePolicy().additionalRequestChance().numerator(),
                  delegating_route_foo->routeEntry()
                      ->hedgePolicy()
                      .additionalRequestChance()
                      .numerator());
        EXPECT_EQ(default_route->routeEntry()->hedgePolicy().initialRequests(),
                  delegating_route_foo->routeEntry()->hedgePolicy().initialRequests());

        EXPECT_EQ(default_route->routeEntry()->priority(),
                  delegating_route_foo->routeEntry()->priority());

        EXPECT_EQ(default_route->routeEntry()->rateLimitPolicy().empty(),
                  delegating_route_foo->routeEntry()->rateLimitPolicy().empty());
        EXPECT_EQ(default_route->routeEntry()->rateLimitPolicy().getApplicableRateLimit(0).empty(),
                  delegating_route_foo->routeEntry()
                      ->rateLimitPolicy()
                      .getApplicableRateLimit(0)
                      .empty());

        EXPECT_EQ(default_route->routeEntry()->retryPolicy().numRetries(),
                  delegating_route_foo->routeEntry()->retryPolicy().numRetries());
        EXPECT_EQ(default_route->routeEntry()->retryPolicy().retryOn(),
                  delegating_route_foo->routeEntry()->retryPolicy().retryOn());

        EXPECT_EQ(default_route->routeEntry()->internalRedirectPolicy().enabled(),
                  delegating_route_foo->routeEntry()->internalRedirectPolicy().enabled());
        EXPECT_EQ(
            default_route->routeEntry()->internalRedirectPolicy().shouldRedirectForResponseCode(
                Code::OK),
            delegating_route_foo->routeEntry()
                ->internalRedirectPolicy()
                .shouldRedirectForResponseCode(Code::OK));

        EXPECT_EQ(default_route->routeEntry()->retryShadowBufferLimit(),
                  delegating_route_foo->routeEntry()->retryShadowBufferLimit());
        EXPECT_EQ(default_route->routeEntry()->shadowPolicies().empty(),
                  delegating_route_foo->routeEntry()->shadowPolicies().empty());
        EXPECT_EQ(default_route->routeEntry()->timeout(),
                  delegating_route_foo->routeEntry()->timeout());
        EXPECT_EQ(default_route->routeEntry()->idleTimeout(),
                  delegating_route_foo->routeEntry()->idleTimeout());
        EXPECT_EQ(default_route->routeEntry()->usingNewTimeouts(),
                  delegating_route_foo->routeEntry()->usingNewTimeouts());
        EXPECT_EQ(default_route->routeEntry()->maxStreamDuration(),
                  delegating_route_foo->routeEntry()->maxStreamDuration());
        EXPECT_EQ(default_route->routeEntry()->grpcTimeoutHeaderMax(),
                  delegating_route_foo->routeEntry()->grpcTimeoutHeaderMax());
        EXPECT_EQ(default_route->routeEntry()->grpcTimeoutHeaderOffset(),
                  delegating_route_foo->routeEntry()->grpcTimeoutHeaderOffset());
        EXPECT_EQ(default_route->routeEntry()->maxGrpcTimeout(),
                  delegating_route_foo->routeEntry()->maxGrpcTimeout());
        EXPECT_EQ(default_route->routeEntry()->grpcTimeoutOffset(),
                  delegating_route_foo->routeEntry()->grpcTimeoutOffset());
        EXPECT_EQ(default_route->virtualHost().virtualCluster(test_req_headers),
                  delegating_route_foo->virtualHost().virtualCluster(test_req_headers));

        EXPECT_EQ(default_route->virtualHost().corsPolicy(),
                  delegating_route_foo->virtualHost().corsPolicy());
        EXPECT_EQ(default_route->virtualHost().rateLimitPolicy().empty(),
                  delegating_route_foo->virtualHost().rateLimitPolicy().empty());

        EXPECT_EQ(default_route->routeEntry()->autoHostRewrite(),
                  delegating_route_foo->routeEntry()->autoHostRewrite());
        EXPECT_EQ(default_route->routeEntry()->metadataMatchCriteria(),
                  delegating_route_foo->routeEntry()->metadataMatchCriteria());
        EXPECT_EQ(default_route->routeEntry()->opaqueConfig(),
                  delegating_route_foo->routeEntry()->opaqueConfig());
        EXPECT_EQ(default_route->routeEntry()->includeVirtualHostRateLimits(),
                  delegating_route_foo->routeEntry()->includeVirtualHostRateLimits());

        // "The mock function has no default action set, and its return type has no default value
        // set"
        EXPECT_EQ(default_route->routeEntry()->tlsContextMatchCriteria(),
                  delegating_route_foo->routeEntry()->tlsContextMatchCriteria());

        EXPECT_EQ(default_route->routeEntry()->pathMatchCriterion().matcher(),
                  delegating_route_foo->routeEntry()->pathMatchCriterion().matcher());
        EXPECT_EQ(default_route->routeEntry()->pathMatchCriterion().matchType(),
                  delegating_route_foo->routeEntry()->pathMatchCriterion().matchType());

        EXPECT_EQ(default_route->routeEntry()->includeAttemptCountInRequest(),
                  delegating_route_foo->routeEntry()->includeAttemptCountInRequest());
        EXPECT_EQ(default_route->routeEntry()->includeAttemptCountInResponse(),
                  delegating_route_foo->routeEntry()->includeAttemptCountInResponse());
        EXPECT_EQ(default_route->routeEntry()->upgradeMap(),
                  delegating_route_foo->routeEntry()->upgradeMap());

        EXPECT_EQ(default_route->routeEntry()->connectConfig().has_value(),
                  delegating_route_foo->routeEntry()->connectConfig().has_value());
        if (default_route->routeEntry()->connectConfig().has_value()) {
          EXPECT_EQ(default_route->routeEntry()->connectConfig()->allow_post(),
                    delegating_route_foo->routeEntry()->connectConfig()->allow_post());
        }

        EXPECT_EQ(default_route->routeName(), delegating_route_foo->routeName());

        // Coverage for finalizeRequestHeaders
        NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info;
        delegating_route_foo->routeEntry()->finalizeRequestHeaders(test_req_headers, stream_info,
                                                                   true);
        EXPECT_EQ("/new_endpoint/foo", test_req_headers.get_(Http::Headers::get().Path));

        // Coverage for finalizeResponseHeaders
        Http::TestResponseHeaderMapImpl test_resp_headers;
        delegating_route_foo->routeEntry()->finalizeResponseHeaders(test_resp_headers, stream_info);
        EXPECT_EQ(test_resp_headers, Http::TestResponseHeaderMapImpl{});

        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  // Kick off the incoming data. end_stream set to true to indicate this is a header only request.
  startRequest(true);

  // Clean up.
  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Filters observe host header w/o port's part when port's removal is configured
TEST_F(HttpConnectionManagerImplTest, FilterShouldUseNormalizedHost) {
  setup();
  // Enable port removal
  strip_port_type_ = Http::StripPortType::MatchingHost;
  const std::string original_host = "host:443";
  const std::string normalized_host = "host";

  auto* filter = new MockStreamFilter();

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));

  EXPECT_CALL(*filter, decodeComplete());
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
  setup();
  // Enable port removal
  strip_port_type_ = Http::StripPortType::MatchingHost;
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
      .WillOnce(Invoke([&](FilterChainManager&) -> bool { return false; }));

  // Kick off the incoming data.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  // Clean up.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

// Observe that we strip the trailing dot.
TEST_F(HttpConnectionManagerImplTest, StripTrailingHostDot) {
  setup();
  // Enable removal of host's trailing dot.
  strip_trailing_host_dot_ = true;
  const std::string original_host = "host.";
  const std::string updated_host = "host";
  // Set up the codec.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->createCodec(fake_input);
  // Create a new stream.
  decoder_ = &conn_manager_->newStream(response_encoder_);
  RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
      {":authority", original_host}, {":path", "/"}, {":method", "GET"}}};
  RequestHeaderMap* updated_headers = headers.get();
  decoder_->decodeHeaders(std::move(headers), true);
  EXPECT_EQ(updated_host, updated_headers->getHostValue());
  // Clean up.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, HostWithoutTrailingDot) {
  setup();
  // Enable removal of host's trailing dot.
  strip_trailing_host_dot_ = true;
  const std::string original_host = "host";
  const std::string updated_host = "host";
  // Set up the codec.
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->createCodec(fake_input);
  // Create a new stream.
  decoder_ = &conn_manager_->newStream(response_encoder_);
  RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
      {":authority", original_host}, {":path", "/"}, {":method", "GET"}}};
  RequestHeaderMap* updated_headers = headers.get();
  decoder_->decodeHeaders(std::move(headers), true);
  EXPECT_EQ(updated_host, updated_headers->getHostValue());
  // Clean up.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, DateHeaderNotPresent) {
  setup();
  setUpEncoderAndDecoder(false, false);
  sendRequestHeadersAndData();
  const auto* modified_headers = sendResponseHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}, {"server", "foo"}}});
  ASSERT_TRUE(modified_headers);
  EXPECT_TRUE(modified_headers->Date());
  doRemoteClose();
}

TEST_F(HttpConnectionManagerImplTest, DateHeaderPresent) {
  setup();
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

TEST_F(HttpConnectionManagerImplTest, StartAndFinishSpanNormalFlow) {
  setup();

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, Tracing::TraceContext&,
                     const StreamInfo::StreamInfo&, const Tracing::Decision) -> Tracing::Span* {
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
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  setup();

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, Tracing::TraceContext&,
                     const StreamInfo::StreamInfo&, const Tracing::Decision) -> Tracing::Span* {
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
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  setup();

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, Tracing::TraceContext&,
                     const StreamInfo::StreamInfo&, const Tracing::Decision) -> Tracing::Span* {
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
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  setup();

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, Tracing::TraceContext&,
                     const StreamInfo::StreamInfo&, const Tracing::Decision) -> Tracing::Span* {
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
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  setup();
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
          Invoke([&](const Tracing::Config& config, Tracing::TraceContext&,
                     const StreamInfo::StreamInfo&, const Tracing::Decision) -> Tracing::Span* {
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
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  setup();
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
          Invoke([&](const Tracing::Config& config, Tracing::TraceContext&,
                     const StreamInfo::StreamInfo&, const Tracing::Decision) -> Tracing::Span* {
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
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  setup();
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
          Invoke([&](const Tracing::Config& config, Tracing::TraceContext&,
                     const StreamInfo::StreamInfo&, const Tracing::Decision) -> Tracing::Span* {
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
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  setup();
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
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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

TEST_F(HttpConnectionManagerImplTest, NoHCMTracingConfigAndActiveSpanWouldBeNullSpan) {
  setup();
  // Null HCM tracing config.
  tracing_config_ = nullptr;

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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

        // Active span always be null span when there is no HCM tracing config.
        EXPECT_EQ(&Tracing::NullSpan::instance(), &filter->callbacks_->activeSpan());
        // Child span should also be null span.
        Tracing::SpanPtr child_span = filter->callbacks_->activeSpan().spawnChild(
            Tracing::EgressConfig::get(), "null_child", test_time_.systemTime());
        Tracing::SpanPtr null_span = std::make_unique<Tracing::NullSpan>();
        auto& child_span_ref = *child_span;
        auto& null_span_ref = *null_span;
        EXPECT_EQ(typeid(child_span_ref).name(), typeid(null_span_ref).name());

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestAccessLog) {
  static constexpr char remote_address[] = "0.0.0.0";
  static constexpr char xff_address[] = "1.2.3.4";

  // stream_info.downstreamRemoteAddress will infer the address from request
  // headers instead of the physical connection
  use_remote_address_ = false;
  setup();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        FilterFactoryCb filter_factory = createDecoderFilterFactoryCb(filter);
        FilterFactoryCb handler_factory = createLogHandlerFactoryCb(handler);

        manager.applyFilterFactoryCb({}, filter_factory);
        manager.applyFilterFactoryCb({}, handler_factory);
        return true;
      }));

  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke([&](const Formatter::HttpFormatterContext&,
                           const StreamInfo::StreamInfo& stream_info) {
        EXPECT_EQ(&decoder_->streamInfo(), &stream_info);
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_EQ(stream_info.responseCode().value(), uint32_t(200));
        EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().localAddress());
        EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().remoteAddress());
        EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().directRemoteAddress());
        EXPECT_NE(nullptr, stream_info.route());

        EXPECT_EQ(stream_info.downstreamAddressProvider().remoteAddress()->ip()->addressAsString(),
                  xff_address);
        EXPECT_EQ(
            stream_info.downstreamAddressProvider().directRemoteAddress()->ip()->addressAsString(),
            remote_address);
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
  setup();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        FilterFactoryCb filter_factory = createDecoderFilterFactoryCb(filter);
        FilterFactoryCb handler_factory = createLogHandlerFactoryCb(handler);

        manager.applyFilterFactoryCb({}, filter_factory);
        manager.applyFilterFactoryCb({}, handler_factory);
        return true;
      }));

  EXPECT_CALL(*filter, onStreamComplete()).WillOnce(Invoke([&]() {
    ProtobufWkt::Value metadata_value;
    metadata_value.set_string_value("value");
    ProtobufWkt::Struct metadata;
    metadata.mutable_fields()->insert({"field", metadata_value});
    filter->callbacks_->streamInfo().setDynamicMetadata("metadata_key", metadata);
  }));

  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            auto dynamic_meta = stream_info.dynamicMetadata().filter_metadata().at("metadata_key");
            EXPECT_EQ("value", dynamic_meta.fields().at("field").string_value());
          }));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":method", "GET"}, {":authority", "host"}, {":path", "/"}}};
        decoder->decodeHeaders(std::move(headers), true);

        filter->callbacks_->streamInfo().setResponseCodeDetails("");
        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
        response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();

        data.drain(4);
        return Http::okStatus();
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

TEST_F(HttpConnectionManagerImplTest, TestRemoteDownstreamDisconnectAccessLog) {
  setup();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        FilterFactoryCb filter_factory = createDecoderFilterFactoryCb(filter);
        FilterFactoryCb handler_factory = createLogHandlerFactoryCb(handler);

        manager.applyFilterFactoryCb({}, filter_factory);
        manager.applyFilterFactoryCb({}, handler_factory);
        return true;
      }));

  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_FALSE(stream_info.responseCode());
            EXPECT_TRUE(stream_info.hasAnyResponseFlag());
            EXPECT_TRUE(stream_info.hasResponseFlag(
                StreamInfo::CoreResponseFlag::DownstreamConnectionTermination));
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

TEST_F(HttpConnectionManagerImplTest, TestLocalDownstreamDisconnectAccessLog) {
  setup();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        FilterFactoryCb filter_factory = createDecoderFilterFactoryCb(filter);
        FilterFactoryCb handler_factory = createLogHandlerFactoryCb(handler);

        manager.applyFilterFactoryCb({}, filter_factory);
        manager.applyFilterFactoryCb({}, handler_factory);
        return true;
      }));

  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_EQ("downstream_local_disconnect(reason_for_local_close)",
                      stream_info.responseCodeDetails().value());
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

  filter_callbacks_.connection_.close(Network::ConnectionCloseType::NoFlush,
                                      "reason for local close");
}

TEST_F(HttpConnectionManagerImplTest, TestAccessLogWithTrailers) {
  setup();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        FilterFactoryCb filter_factory = createDecoderFilterFactoryCb(filter);
        FilterFactoryCb handler_factory = createLogHandlerFactoryCb(handler);

        manager.applyFilterFactoryCb({}, filter_factory);
        manager.applyFilterFactoryCb({}, handler_factory);
        return true;
      }));

  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_TRUE(stream_info.responseCode());
            EXPECT_EQ(stream_info.responseCode().value(), uint32_t(200));
            EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().localAddress());
            EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().remoteAddress());
            EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().directRemoteAddress());
            EXPECT_NE(nullptr, stream_info.route());
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
  setup();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        FilterFactoryCb filter_factory = createDecoderFilterFactoryCb(filter);
        FilterFactoryCb handler_factory = createLogHandlerFactoryCb(handler);

        manager.applyFilterFactoryCb({}, filter_factory);
        manager.applyFilterFactoryCb({}, handler_factory);
        return true;
      }));

  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke([this](const Formatter::HttpFormatterContext&,
                              const StreamInfo::StreamInfo& stream_info) {
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_EQ(stream_info.responseCode().value(), uint32_t(400));
        EXPECT_EQ("missing_host_header", stream_info.responseCodeDetails().value());
        EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().localAddress());
        EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().remoteAddress());
        EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().directRemoteAddress());
        // Even the request is invalid, will still try to find a route before response filter chain
        // path.
        EXPECT_EQ(route_config_provider_.route_config_->route_, stream_info.route());
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

TEST_F(HttpConnectionManagerImplTest, TestAccessLogOnNewRequest) {
  setup();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        FilterFactoryCb filter_factory = createDecoderFilterFactoryCb(filter);
        FilterFactoryCb handler_factory = createLogHandlerFactoryCb(handler);

        manager.applyFilterFactoryCb({}, filter_factory);
        manager.applyFilterFactoryCb({}, handler_factory);
        return true;
      }));

  flush_access_log_on_new_request_ = true;

  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke([](const Formatter::HttpFormatterContext& log_context,
                          const StreamInfo::StreamInfo& stream_info) {
        // First call to log() is made when a new HTTP request has been received
        // On the first call it is expected that there is no response code.
        EXPECT_EQ(AccessLog::AccessLogType::DownstreamStart, log_context.accessLogType());
        EXPECT_FALSE(stream_info.responseCode());
      }))
      .WillOnce(Invoke([](const Formatter::HttpFormatterContext& log_context,
                          const StreamInfo::StreamInfo& stream_info) {
        // Second call to log() is made when filter is destroyed, so it is expected
        // that the response code is available and matches the response headers.
        EXPECT_EQ(AccessLog::AccessLogType::DownstreamEnd, log_context.accessLogType());
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_EQ(stream_info.responseCode().value(), uint32_t(200));
        EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().localAddress());
        EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().remoteAddress());
        EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().directRemoteAddress());
        EXPECT_NE(nullptr, stream_info.route());
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

TEST_F(HttpConnectionManagerImplTest, TestAccessLogOnTunnelEstablished) {
  setup();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createUpgradeFilterChain("CONNECT", _, _, _))
      .WillOnce(Invoke([&](absl::string_view, const FilterChainFactory::UpgradeMap*,
                           FilterChainManager& manager, const Http::FilterChainOptions&) -> bool {
        FilterFactoryCb filter_factory = createDecoderFilterFactoryCb(filter);
        FilterFactoryCb handler_factory = createLogHandlerFactoryCb(handler);
        manager.applyFilterFactoryCb({}, filter_factory);
        manager.applyFilterFactoryCb({}, handler_factory);
        return true;
      }));

  flush_log_on_tunnel_successfully_established_ = true;

  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke([](const Formatter::HttpFormatterContext& log_context,
                          const StreamInfo::StreamInfo& stream_info) {
        // First call to log() is made when a new HTTP tunnel has been established.
        EXPECT_EQ(log_context.accessLogType(),
                  AccessLog::AccessLogType::DownstreamTunnelSuccessfullyEstablished);
        EXPECT_EQ(stream_info.responseCode().value(), uint32_t(200));
      }))
      .WillOnce(Invoke([](const Formatter::HttpFormatterContext& log_context,
                          const StreamInfo::StreamInfo& stream_info) {
        // Second call to log() is made when the request is completed, so it is expected
        // that the response code is available and matches the response headers.
        EXPECT_EQ(AccessLog::AccessLogType::DownstreamEnd, log_context.accessLogType());
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_EQ(stream_info.responseCode().value(), uint32_t(200));
        EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().localAddress());
        EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().remoteAddress());
        EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().directRemoteAddress());
        EXPECT_NE(nullptr, stream_info.route());
      }));

  EXPECT_CALL(*codec_, dispatch(_))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> Http::Status {
        decoder_ = &conn_manager_->newStream(response_encoder_);

        RequestHeaderMapPtr headers{
            new TestRequestHeaderMapImpl{{":method", "CONNECT"}, {":authority", "host"}}};
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

TEST_F(HttpConnectionManagerImplTest, TestPeriodicAccessLogging) {
  access_log_flush_interval_ = std::chrono::milliseconds(30 * 1000);
  stream_idle_timeout_ = std::chrono::milliseconds(0);
  request_timeout_ = std::chrono::milliseconds(0);
  request_headers_timeout_ = std::chrono::milliseconds(0);
  max_stream_duration_ = std::nullopt;

  setup(SetupOpts().setServerName("server-opts"));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        FilterFactoryCb filter_factory = createDecoderFilterFactoryCb(filter);
        FilterFactoryCb handler_factory = createLogHandlerFactoryCb(handler);

        manager.applyFilterFactoryCb({}, filter_factory);
        manager.applyFilterFactoryCb({}, handler_factory);
        return true;
      }));
  Event::MockTimer* periodic_log_timer;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    response_encoder_.stream_.bytes_meter_->addWireBytesReceived(4);
    periodic_log_timer = setUpTimer();
    EXPECT_CALL(*periodic_log_timer, enableTimer(*access_log_flush_interval_, _));
    decoder_ = &conn_manager_->newStream(response_encoder_);

    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":authority", "host"},
                                     {":path", "/"},
                                     {"x-request-id", "125a4afb-6f55-a4ba-ad80-413f09f48a28"}}};
    decoder_->decodeHeaders(std::move(headers), true);

    data.drain(4);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke([&](const Formatter::HttpFormatterContext& log_context,
                           const StreamInfo::StreamInfo& stream_info) {
        EXPECT_EQ(AccessLog::AccessLogType::DownstreamPeriodic, log_context.accessLogType());
        EXPECT_EQ(&decoder_->streamInfo(), &stream_info);
        EXPECT_EQ(stream_info.requestComplete(), absl::nullopt);
        EXPECT_THAT(stream_info.getDownstreamBytesMeter()->bytesAtLastDownstreamPeriodicLog(),
                    testing::IsNull());
      }))
      .WillOnce(Invoke([](const Formatter::HttpFormatterContext& log_context,
                          const StreamInfo::StreamInfo& stream_info) {
        EXPECT_EQ(AccessLog::AccessLogType::DownstreamPeriodic, log_context.accessLogType());
        EXPECT_EQ(stream_info.getDownstreamBytesMeter()
                      ->bytesAtLastDownstreamPeriodicLog()
                      ->wire_bytes_received,
                  4);
      }));
  // Pretend like some 30s has passed, and the log should be written.
  EXPECT_CALL(*periodic_log_timer, enableTimer(*access_log_flush_interval_, _)).Times(2);
  periodic_log_timer->invokeCallback();
  // Add additional bytes.
  response_encoder_.stream_.bytes_meter_->addWireBytesReceived(12);
  periodic_log_timer->invokeCallback();
  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke([&](const Formatter::HttpFormatterContext& log_context,
                           const StreamInfo::StreamInfo& stream_info) {
        EXPECT_EQ(AccessLog::AccessLogType::DownstreamEnd, log_context.accessLogType());
        EXPECT_EQ(&decoder_->streamInfo(), &stream_info);
        EXPECT_THAT(stream_info.responseCodeDetails(),
                    testing::Optional(testing::StrEq("details")));
        EXPECT_THAT(stream_info.responseCode(), testing::Optional(200));
        EXPECT_EQ(stream_info.getDownstreamBytesMeter()
                      ->bytesAtLastDownstreamPeriodicLog()
                      ->wire_bytes_received,
                  4 + 12);
      }));
  filter->callbacks_->streamInfo().setResponseCodeDetails("");
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  EXPECT_CALL(*periodic_log_timer, disableTimer);
  filter->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
}

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
        .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
          auto factory = createStreamFilterFactoryCb(StreamFilterSharedPtr{filter});
          manager.applyFilterFactoryCb({}, factory);
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

TEST_F(HttpConnectionManagerImplTest, TestAccessLogSsl) {
  setup(SetupOpts().setSsl(true));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<AccessLog::MockInstance> handler(new NiceMock<AccessLog::MockInstance>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        FilterFactoryCb filter_factory = createDecoderFilterFactoryCb(filter);
        FilterFactoryCb handler_factory = createLogHandlerFactoryCb(handler);

        manager.applyFilterFactoryCb({}, filter_factory);
        manager.applyFilterFactoryCb({}, handler_factory);
        return true;
      }));

  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke(
          [](const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo& stream_info) {
            EXPECT_TRUE(stream_info.responseCode());
            EXPECT_EQ(stream_info.responseCode().value(), uint32_t(200));
            EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().localAddress());
            EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().remoteAddress());
            EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().directRemoteAddress());
            EXPECT_NE(nullptr, stream_info.downstreamAddressProvider().sslConnection());
            EXPECT_NE(nullptr, stream_info.route());
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
  setup();

  // Disable tracing.
  tracing_config_.reset();

  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _)).Times(0);
  ON_CALL(runtime_.snapshot_, featureEnabled("tracing.global_enabled",
                                             An<const envoy::type::v3::FractionalPercent&>(), _))
      .WillByDefault(Return(true));

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  setup();

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
  setup();

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
  setup();

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
  setup();

  std::shared_ptr<MockStreamEncoderFilter> filter(new NiceMock<MockStreamEncoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        FilterFactoryCb factory = createEncoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  setup();

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

  EXPECT_CALL(*handler, log(_, _))
      .WillOnce(Invoke([](const Formatter::HttpFormatterContext&,
                          const StreamInfo::StreamInfo& stream_info) {
        EXPECT_TRUE(stream_info.responseCode());
        EXPECT_TRUE(stream_info.hasAnyResponseFlag());
        EXPECT_TRUE(stream_info.hasResponseFlag(StreamInfo::CoreResponseFlag::StreamIdleTimeout));
      }));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        FilterFactoryCb filter_factory = createDecoderFilterFactoryCb(filter);
        FilterFactoryCb handler_factory = createLogHandlerFactoryCb(handler);

        manager.applyFilterFactoryCb({}, filter_factory);
        manager.applyFilterFactoryCb({}, handler_factory);
        return true;
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_EQ("stream timeout", response_body);
  EXPECT_EQ(1U, stats_.named_.downstream_rq_idle_timeout_.value());
}

// Test timeout variants.
TEST_F(HttpConnectionManagerImplTest, DurationTimeout) {
  stream_idle_timeout_ = std::chrono::milliseconds(10);
  setup();
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
  decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
  decoder_filters_[0]->callbacks_->clusterInfo();

  Event::MockTimer* timer = setUpTimer();

  // Set a max duration of 30ms and make sure a 30ms timer is set.
  {
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(30), _));
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, maxStreamDuration())
        .Times(2)
        .WillRepeatedly(Return(std::chrono::milliseconds(30)));
    decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // Clear the timeout and make sure the timer is disabled.
  {
    EXPECT_CALL(*timer, disableTimer());
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, maxStreamDuration())
        .Times(1)
        .WillRepeatedly(Return(absl::nullopt));
    decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // With no route timeout, but HCM defaults, the HCM defaults will be used.
  {
    max_stream_duration_ = std::chrono::milliseconds(17);
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(17), _));
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, maxStreamDuration())
        .Times(1)
        .WillRepeatedly(Return(absl::nullopt));
    decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
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
    decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // With a gRPC header of 1M and a gRPC header max of 0, respect the gRPC header.
  {
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, grpcTimeoutHeaderMax())
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::chrono::milliseconds(0)));
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(60000), _));
    decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // With a gRPC header and a larger gRPC header cap, respect the gRPC header.
  {
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, grpcTimeoutHeaderMax())
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::chrono::milliseconds(20000000)));
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(60000), _));
    decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // With a gRPC header and a small gRPC header cap, use the cap.
  {
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, grpcTimeoutHeaderMax())
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::chrono::milliseconds(20)));
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(20), _));
    decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  latched_headers->setGrpcTimeout("0m");
  // With a gRPC header of 0, use the header
  {
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, grpcTimeoutHeaderMax())
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::chrono::milliseconds(20)));
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(0), _));
    decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
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
    decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
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
    decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
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
    decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
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
    decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
    decoder_filters_[0]->callbacks_->clusterInfo();
  }

  // With an invalid gRPC timeout, refreshing cached route will not use header and use stream
  // duration.
  latched_headers->setGrpcTimeout("6666666666666H");
  {
    // 25ms used already from previous case so timer is set to be 5ms.
    EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(5), _));
    EXPECT_CALL(route_config_provider_.route_config_->route_->route_entry_, maxStreamDuration())
        .Times(2)
        .WillRepeatedly(Return(std::chrono::milliseconds(30)));
    decoder_filters_[0]->callbacks_->downstreamCallbacks()->clearRouteCache();
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
  setup();
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
  setup();
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
  setup();
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
  setup();
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
  setup();
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
  setup();
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(10)));

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  setup();
  ON_CALL(route_config_provider_.route_config_->route_->route_entry_, idleTimeout())
      .WillByDefault(Return(std::chrono::milliseconds(10)));
  proxy_100_continue_ = true;

  // Store the basic request encoder during filter chain setup.
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
    filter->callbacks_->encode1xxHeaders(std::move(response_continue_headers));

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
  EXPECT_CALL(response_encoder_, encode1xxHeaders(_));

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
  setup();

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
  setup();

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
  setup();

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
  setup();

  std::string response_body;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _));
    EXPECT_CALL(*request_timer, disableTimer()).Times(AtLeast(1));

    EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
        .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
          EXPECT_EQ("408", headers.getStatusValue());
        }));
    EXPECT_CALL(response_encoder_, encodeData(_, true)).WillOnce(AddBufferToString(&response_body));

    conn_manager_->newStream(response_encoder_);
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, pushTrackedObject(_));
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, popTrackedObject(_));
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
  setup();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _));
    EXPECT_CALL(*request_timer, disableTimer());

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
  setup();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _));

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
  setup();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _));

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
  setup();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _));
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
  setup();
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        return true;
      }));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, _));

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    Event::MockTimer* request_timer = setUpTimer();
    EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _));

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
  setup();

  Event::MockTimer* request_timer = setUpTimer();
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

    decoder_->decodeHeaders(std::move(headers), false);
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("1234");

  EXPECT_CALL(*request_timer, enableTimer(request_timeout_, _));
  conn_manager_->onData(fake_input, false); // kick off request

  EXPECT_CALL(*request_timer, disableTimer());
  EXPECT_EQ(0U, stats_.named_.downstream_rq_timeout_.value());

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestHeaderTimeoutDisarmedAfterHeaders) {
  request_headers_timeout_ = std::chrono::milliseconds(10);
  setup();

  Event::MockTimer* request_header_timer;
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
        request_header_timer = setUpTimer();
        EXPECT_CALL(*request_header_timer, enableTimer(request_headers_timeout_, _));

        decoder_ = &conn_manager_->newStream(response_encoder_);
        return Http::okStatus();
      }))
      .WillOnce(Return(Http::okStatus()))
      .WillOnce([&](Buffer::Instance&) {
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "localhost:8080"}, {":path", "/"}, {":method", "GET"}}};

        EXPECT_CALL(*request_header_timer, disableTimer);
        decoder_->decodeHeaders(std::move(headers), false);
        return Http::okStatus();
      });

  Buffer::OwnedImpl first_line("GET /HTTP/1.1\r\n");
  Buffer::OwnedImpl second_line("Host: localhost:8080\r\n");
  Buffer::OwnedImpl empty_line("\r\n");
  conn_manager_->onData(first_line, false);
  EXPECT_TRUE(request_header_timer->enabled_);
  conn_manager_->onData(second_line, false);
  EXPECT_TRUE(request_header_timer->enabled_);
  conn_manager_->onData(empty_line, false);
  Mock::VerifyAndClearExpectations(codec_);
  Mock::VerifyAndClearExpectations(request_header_timer);

  expectOnDestroy();
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(HttpConnectionManagerImplTest, RequestHeaderTimeoutCallbackDisarmsAndReturns408) {
  request_headers_timeout_ = std::chrono::milliseconds(10);
  setup();

  Event::MockTimer* request_header_timer;
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    request_header_timer = setUpTimer();
    EXPECT_CALL(*request_header_timer, enableTimer(request_headers_timeout_, _));

    conn_manager_->newStream(response_encoder_);
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, pushTrackedObject(_));
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, popTrackedObject(_));
    return Http::okStatus();
  }));

  Buffer::OwnedImpl fake_input("GET /resource HTTP/1.1\r\n\r\n");
  conn_manager_->onData(fake_input, false); // kick off request

  // The client took too long to send headers.
  EXPECT_CALL(*request_header_timer, disableTimer);
  request_header_timer->invokeCallback();

  EXPECT_EQ(1U, stats_.named_.downstream_rq_header_timeout_.value());
}

TEST_F(HttpConnectionManagerImplTest, MaxStreamDurationDisabledIfSetToZero) {
  max_stream_duration_ = std::chrono::milliseconds(0);
  setup();

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
  setup();

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
  setup();
  Event::MockTimer* duration_timer = setUpTimer();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    EXPECT_CALL(*duration_timer, enableTimer(max_stream_duration_.value(), _));
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

TEST_F(HttpConnectionManagerImplTest, MaxStreamDurationFiredReturn408IfRequestWasNotComplete) {
  max_stream_duration_ = std::chrono::milliseconds(10);
  setup();
  Event::MockTimer* duration_timer = setUpTimer();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    EXPECT_CALL(*duration_timer, enableTimer(max_stream_duration_.value(), _)).Times(2);
    decoder_ = &conn_manager_->newStream(response_encoder_);

    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "localhost:8080"}, {":path", "/"}, {":method", "GET"}}};
    // The codec will dispatch data after the header.
    decoder_->decodeHeaders(std::move(headers), false);
    data.drain(4);
    return Http::okStatus();
  }));
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*duration_timer, disableTimer());
  // Response code 408 after downstream max stream timeout because the request is not fully read by
  // the decoder.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("408", headers.getStatusValue());
      }));
  EXPECT_CALL(response_encoder_, encodeData(_, true));
  duration_timer->invokeCallback();
}

TEST_F(HttpConnectionManagerImplTest, MaxStreamDurationFiredReturn504IfRequestWasFullyRead) {
  max_stream_duration_ = std::chrono::milliseconds(10);
  setup();
  Event::MockTimer* duration_timer = setUpTimer();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    EXPECT_CALL(*duration_timer, enableTimer(max_stream_duration_.value(), _)).Times(2);
    decoder_ = &conn_manager_->newStream(response_encoder_);

    RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
        {":authority", "localhost:8080"}, {":path", "/"}, {":method", "GET"}}};
    // This is a header only request.
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));
  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);

  EXPECT_CALL(*duration_timer, disableTimer());
  // 504 direct response after downstream max stream timeout because the request is fully read.
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false))
      .WillOnce(Invoke([](const ResponseHeaderMap& headers, bool) -> void {
        EXPECT_EQ("504", headers.getStatusValue());
      }));
  EXPECT_CALL(response_encoder_, encodeData(_, true));
  duration_timer->invokeCallback();
}

TEST_F(HttpConnectionManagerImplTest, MaxStreamDurationCallbackNotCalledIfResetStreamValidly) {
  max_stream_duration_ = std::chrono::milliseconds(5000);
  setup();
  Event::MockTimer* duration_timer = setUpTimer();

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    EXPECT_CALL(*duration_timer, enableTimer(max_stream_duration_.value(), _));
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
  setup();
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
  setup(SetupOpts().setTracing(false));

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

  EXPECT_CALL(filter_factory_, createUpgradeFilterChain(_, _, _, _))
      .WillRepeatedly(
          Invoke([&](absl::string_view, const Http::FilterChainFactory::UpgradeMap*,
                     FilterChainManager& manager, const Http::FilterChainOptions&) -> bool {
            auto factory = createStreamFilterFactoryCb(StreamFilterSharedPtr{filter});
            manager.applyFilterFactoryCb({}, factory);
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
  setup(SetupOpts().setTracing(false));

  EXPECT_CALL(filter_factory_, createUpgradeFilterChain("CONNECT", _, _, _))
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
  setup(SetupOpts().setTracing(false));

  EXPECT_CALL(filter_factory_, createUpgradeFilterChain("CONNECT", _, _, _))
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

// Regression test for https://github.com/envoyproxy/envoy/issues/10138
TEST_F(HttpConnectionManagerImplTest, DrainCloseRaceWithClose) {
  InSequence s;
  setup();

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
  response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();

  // Fake a protocol error that races with the drain timeout. This will cause a local close.
  // Also fake the local close not closing immediately.
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Return(codecProtocolError("protocol error")));
  EXPECT_CALL(*drain_timer, disableTimer());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay, _))
      .WillOnce(Return());
  conn_manager_->onData(fake_input, false);

  // Now fire the close event which should have no effect as all close work has already been done.
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::LocalClose);
}

TEST_F(HttpConnectionManagerImplTest,
       FilterThatWaitsForBodyCanBeCalledAfterFilterThatAddsBodyEvenIfItIsNotLast) {
  InSequence s;
  setup();

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
  setup(SetupOpts().setSsl(true));

  MockStreamDecoderFilter* filter = new NiceMock<MockStreamDecoderFilter>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce(Invoke([&](FilterChainManager& manager) -> bool {
        auto factory = createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr{filter});
        manager.applyFilterFactoryCb({}, factory);
        return true;
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
  response_encoder_.stream_.codec_callbacks_->onCodecEncodeComplete();
  EXPECT_EQ(ssl_connection_.get(), filter->callbacks_->connection()->ssl().get());

  EXPECT_CALL(*codec_, goAway());
  EXPECT_CALL(filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::FlushWriteAndDelay, _));
  EXPECT_CALL(*drain_timer, disableTimer());
  drain_timer->invokeCallback();

  EXPECT_EQ(1U, stats_.named_.downstream_cx_drain_close_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_3xx_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_3xx_.value());
  EXPECT_EQ(1U, stats_.named_.downstream_rq_completed_.value());
  EXPECT_EQ(1U, listener_stats_.downstream_rq_completed_.value());
}

// Tests for the presence/absence and contents of the synthesized Proxy-Status
// HTTP response header. NB: proxy_status_config_ persists in the test base.
class ProxyStatusTest : public HttpConnectionManagerImplTest {
public:
  void initialize() {
    setup(SetupOpts().setServerName(servername_).setTracing(false));
    setUpEncoderAndDecoder(/*request_with_data_and_trailers=*/false,
                           /*decode_headers_stop_all=*/false);
    sendRequestHeadersAndData();
  }
  const ResponseHeaderMap*
  sendRequestWith(int status, StreamInfo::CoreResponseFlag response_flag, std::string details,
                  absl::optional<std::string> proxy_status = absl::nullopt) {
    auto response_headers = new TestResponseHeaderMapImpl{{":status", std::to_string(status)}};
    if (proxy_status.has_value()) {
      response_headers->setProxyStatus(proxy_status.value());
    }
    return sendResponseHeaders(ResponseHeaderMapPtr{response_headers}, response_flag, details);
  }
  void TearDown() override { doRemoteClose(); }

protected:
  const std::string servername_{"custom_server_name"};
};

TEST_F(ProxyStatusTest, NoPopulateProxyStatus) {
  proxy_status_config_ = nullptr;

  initialize();

  const ResponseHeaderMap* altered_headers =
      sendRequestWith(403, StreamInfo::CoreResponseFlag::FailedLocalHealthCheck, "foo");
  ASSERT_TRUE(altered_headers);
  ASSERT_FALSE(altered_headers->ProxyStatus());
  EXPECT_EQ(altered_headers->getStatusValue(), "403"); // unchanged from request.
}

TEST_F(ProxyStatusTest, PopulateProxyStatusWithDetailsAndResponseCodeAndServerName) {
  proxy_status_config_ = std::make_unique<HttpConnectionManagerProto::ProxyStatusConfig>();
  proxy_status_config_->set_remove_details(false);
  proxy_status_config_->set_set_recommended_response_code(true);
  proxy_status_config_->set_use_node_id(true);

  initialize();

  const ResponseHeaderMap* altered_headers =
      sendRequestWith(403, StreamInfo::CoreResponseFlag::FailedLocalHealthCheck, /*details=*/"foo");

  ASSERT_TRUE(altered_headers);
  ASSERT_TRUE(altered_headers->ProxyStatus());
  EXPECT_EQ(altered_headers->getProxyStatusValue(),
            "node_name; error=destination_unavailable; details=\"foo; LH\"");
  // Changed from request, since set_recommended_response_code is true. Here,
  // 503 is the recommended response code for FailedLocalHealthCheck.
  EXPECT_EQ(altered_headers->getStatusValue(), "503");
}

TEST_F(ProxyStatusTest, PopulateProxyStatusWithDetailsAndResponseCode) {
  proxy_status_config_ = std::make_unique<HttpConnectionManagerProto::ProxyStatusConfig>();
  proxy_status_config_->set_remove_details(false);
  proxy_status_config_->set_set_recommended_response_code(true);

  initialize();

  const ResponseHeaderMap* altered_headers =
      sendRequestWith(403, StreamInfo::CoreResponseFlag::UpstreamRequestTimeout, /*details=*/"bar");

  ASSERT_TRUE(altered_headers);
  ASSERT_TRUE(altered_headers->ProxyStatus());
  EXPECT_EQ(altered_headers->getProxyStatusValue(),
            "custom_server_name; error=http_response_timeout; details=\"bar; UT\"");
  // Changed from request, since set_recommended_response_code is true. Here,
  // 504 is the recommended response code for UpstreamRequestTimeout.
  EXPECT_EQ(altered_headers->getStatusValue(), "504");
}

TEST_F(ProxyStatusTest, PopulateUnauthorizedProxyStatus) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.proxy_status_mapping_more_core_response_flags", "true"}});
  proxy_status_config_ = std::make_unique<HttpConnectionManagerProto::ProxyStatusConfig>();
  proxy_status_config_->set_remove_details(false);

  initialize();

  const ResponseHeaderMap* altered_headers = sendRequestWith(
      403, StreamInfo::CoreResponseFlag::UnauthorizedExternalService, /*details=*/"bar");

  ASSERT_TRUE(altered_headers);
  ASSERT_TRUE(altered_headers->ProxyStatus());
  EXPECT_EQ(altered_headers->getProxyStatusValue(),
            "custom_server_name; error=connection_refused; details=\"bar; UAEX\"");
  EXPECT_EQ(altered_headers->getStatusValue(), "403");
}

TEST_F(ProxyStatusTest, NoPopulateUnauthorizedProxyStatus) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.proxy_status_mapping_more_core_response_flags", "false"}});
  proxy_status_config_ = std::make_unique<HttpConnectionManagerProto::ProxyStatusConfig>();
  proxy_status_config_->set_remove_details(false);

  initialize();

  const ResponseHeaderMap* altered_headers = sendRequestWith(
      403, StreamInfo::CoreResponseFlag::UnauthorizedExternalService, /*details=*/"bar");

  ASSERT_TRUE(altered_headers);
  ASSERT_FALSE(altered_headers->ProxyStatus());
  EXPECT_EQ(altered_headers->getProxyStatusValue(), "");
  EXPECT_EQ(altered_headers->getStatusValue(), "403");
}

TEST_F(ProxyStatusTest, PopulateProxyStatusWithDetails) {
  TestScopedRuntime scoped_runtime;
  proxy_status_config_ = std::make_unique<HttpConnectionManagerProto::ProxyStatusConfig>();
  proxy_status_config_->set_remove_details(false);
  proxy_status_config_->set_remove_connection_termination_details(false);
  proxy_status_config_->set_remove_response_flags(false);
  proxy_status_config_->set_set_recommended_response_code(false);

  initialize();

  const ResponseHeaderMap* altered_headers =
      sendRequestWith(403, StreamInfo::CoreResponseFlag::UpstreamRequestTimeout, /*details=*/"bar");

  ASSERT_TRUE(altered_headers);
  ASSERT_TRUE(altered_headers->ProxyStatus());
  EXPECT_EQ(altered_headers->getProxyStatusValue(),
            "custom_server_name; error=http_response_timeout; details=\"bar; UT\"");
  // Unchanged from request, since set_recommended_response_code is false. Here,
  // 504 would be the recommended response code for UpstreamRequestTimeout,
  EXPECT_NE(altered_headers->getStatusValue(), "504");
  EXPECT_EQ(altered_headers->getStatusValue(), "403");
}

TEST_F(ProxyStatusTest, PopulateProxyStatusWithoutDetails) {
  TestScopedRuntime scoped_runtime;
  proxy_status_config_ = std::make_unique<HttpConnectionManagerProto::ProxyStatusConfig>();
  proxy_status_config_->set_remove_details(true);
  proxy_status_config_->set_set_recommended_response_code(false);

  initialize();

  const ResponseHeaderMap* altered_headers =
      sendRequestWith(403, StreamInfo::CoreResponseFlag::UpstreamRequestTimeout, /*details=*/"baz");

  ASSERT_TRUE(altered_headers);
  ASSERT_TRUE(altered_headers->ProxyStatus());
  EXPECT_EQ(altered_headers->getProxyStatusValue(),
            "custom_server_name; error=http_response_timeout");
  // Unchanged.
  EXPECT_EQ(altered_headers->getStatusValue(), "403");
  // Since remove_details=true, we should not have "baz", the value of
  // response_code_details, in the Proxy-Status header.
  EXPECT_THAT(altered_headers->getProxyStatusValue(), Not(HasSubstr("baz")));
}

TEST_F(ProxyStatusTest, PopulateProxyStatusAppendToPreviousValue) {
  TestScopedRuntime scoped_runtime;
  proxy_status_config_ = std::make_unique<HttpConnectionManagerProto::ProxyStatusConfig>();
  proxy_status_config_->set_remove_details(false);

  initialize();

  const ResponseHeaderMap* altered_headers =
      sendRequestWith(403, StreamInfo::CoreResponseFlag::UpstreamRequestTimeout, /*details=*/"baz",
                      /*proxy_status=*/"SomeCDN");

  ASSERT_TRUE(altered_headers);
  ASSERT_TRUE(altered_headers->ProxyStatus());
  // Expect to see the appended previous value: "SomeCDN; custom_server_name; ...".
  EXPECT_EQ(altered_headers->getProxyStatusValue(),
            "SomeCDN, custom_server_name; error=http_response_timeout; details=\"baz; UT\"");
}

TEST_F(HttpConnectionManagerImplTest, TestFilterAccessLogBeforeConfigAccessLog) {
  log_handler_ = std::make_shared<NiceMock<AccessLog::MockInstance>>(); // filter log handler
  std::shared_ptr<AccessLog::MockInstance> handler(
      new NiceMock<AccessLog::MockInstance>()); // config log handler
  access_logs_ = {handler};
  setup();
  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  startRequest();

  {
    InSequence s; // Create an InSequence object to enforce order

    EXPECT_CALL(*log_handler_, log(_, _))
        .WillOnce(Invoke([](const Formatter::HttpFormatterContext& log_context,
                            const StreamInfo::StreamInfo& stream_info) {
          EXPECT_EQ(AccessLog::AccessLogType::DownstreamEnd, log_context.accessLogType());
          EXPECT_FALSE(stream_info.hasAnyResponseFlag());
        }));

    EXPECT_CALL(*handler, log(_, _))
        .WillOnce(Invoke([](const Formatter::HttpFormatterContext& log_context,
                            const StreamInfo::StreamInfo& stream_info) {
          // First call to log() is made when a new HTTP request has been received
          // On the first call it is expected that there is no response code.
          EXPECT_EQ(AccessLog::AccessLogType::DownstreamEnd, log_context.accessLogType());
          EXPECT_TRUE(stream_info.responseCode());
        }));
  }

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
}

TEST_F(HttpConnectionManagerImplTest, TestFilterAccessLogBeforeConfigAccessLogFeatureFalse) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.filter_access_loggers_first", "false"}});
  log_handler_ = std::make_shared<NiceMock<AccessLog::MockInstance>>(); // filter log handler
  std::shared_ptr<AccessLog::MockInstance> handler(
      new NiceMock<AccessLog::MockInstance>()); // config log handler
  access_logs_ = {handler};
  setup();
  setupFilterChain(1, 0);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  startRequest();

  {
    InSequence s; // Create an InSequence object to enforce order

    EXPECT_CALL(*handler, log(_, _))
        .WillOnce(Invoke([](const Formatter::HttpFormatterContext& log_context,
                            const StreamInfo::StreamInfo& stream_info) {
          // First call to log() is made when a new HTTP request has been received
          // On the first call it is expected that there is no response code.
          EXPECT_EQ(AccessLog::AccessLogType::DownstreamEnd, log_context.accessLogType());
          EXPECT_TRUE(stream_info.responseCode());
        }));

    EXPECT_CALL(*log_handler_, log(_, _))
        .WillOnce(Invoke([](const Formatter::HttpFormatterContext& log_context,
                            const StreamInfo::StreamInfo& stream_info) {
          EXPECT_EQ(AccessLog::AccessLogType::DownstreamEnd, log_context.accessLogType());
          EXPECT_FALSE(stream_info.hasAnyResponseFlag());
        }));
  }

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), true, "details");
}

} // namespace Http
} // namespace Envoy
