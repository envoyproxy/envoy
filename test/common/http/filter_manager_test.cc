#include "envoy/stream_info/filter_state.h"

#include "common/http/filter_manager.h"
#include "common/stream_info/filter_state_impl.h"
#include "common/stream_info/stream_info_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_reply/mocks.h"
#include "test/mocks/network/mocks.h"

#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Http {
namespace {
class FilterManagerTest : public testing::Test {
public:
  void initialize() {
    filter_manager_ = std::make_unique<FilterManager>(
        filter_manager_callbacks_, dispatcher_, connection_, 0, true, 10000, filter_factory_,
        local_reply_, protocol_, time_source_, filter_state_,
        StreamInfo::FilterState::LifeSpan::Connection);
  }

  std::unique_ptr<FilterManager> filter_manager_;
  NiceMock<MockFilterManagerCallbacks> filter_manager_callbacks_;
  Event::MockDispatcher dispatcher_;
  NiceMock<Network::MockConnection> connection_;
  Envoy::Http::MockFilterChainFactory filter_factory_;
  LocalReply::MockLocalReply local_reply_;
  Protocol protocol_{Protocol::Http2};
  NiceMock<MockTimeSystem> time_source_;
  StreamInfo::FilterStateSharedPtr filter_state_ =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
};

// Verifies that the local reply persists the gRPC classification even if the request headers are
// modified.
TEST_F(FilterManagerTest, SendLocalReplyDuringDecodingGrpcClassiciation) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillRepeatedly(Invoke([&](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        headers.setContentType("text/plain");

        filter->callbacks_->sendLocalReply(Code::InternalServerError, "", nullptr, absl::nullopt,
                                           "");

        return FilterHeadersStatus::StopIteration;
      }));

  RequestHeaderMapPtr grpc_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"},
                                   {":path", "/"},
                                   {":method", "GET"},
                                   {"content-type", "application/grpc"}}};

  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(absl::make_optional(std::ref(*grpc_headers))));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(filter);
      }));

  filter_manager_->createFilterChain();

  filter_manager_->requestHeadersInitialized();
  EXPECT_CALL(local_reply_, rewrite(_, _, _, _, _, _));
  EXPECT_CALL(filter_manager_callbacks_, setResponseHeaders_(_))
      .WillOnce(Invoke([](auto& response_headers) {
        EXPECT_THAT(response_headers,
                    HeaderHasValueRef(Http::Headers::get().ContentType, "application/grpc"));
      }));
  EXPECT_CALL(filter_manager_callbacks_, resetIdleTimer());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, _));
  EXPECT_CALL(filter_manager_callbacks_, endStream());
  filter_manager_->decodeHeaders(*grpc_headers, true);
  filter_manager_->destroyFilters();
}

// Verifies that the local reply persists the gRPC classification even if the request headers are
// modified when directly encoding a response.
TEST_F(FilterManagerTest, SendLocalReplyDuringEncodingGrpcClassiciation) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> decoder_filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*decoder_filter, decodeHeaders(_, true))
      .WillRepeatedly(Invoke([&](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        headers.setContentType("text/plain");

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        decoder_filter->callbacks_->encodeHeaders(std::move(response_headers), true, "test");

        return FilterHeadersStatus::StopIteration;
      }));

  std::shared_ptr<MockStreamFilter> encoder_filter(new NiceMock<MockStreamFilter>());

  EXPECT_CALL(*encoder_filter, encodeHeaders(_, true))
      .WillRepeatedly(Invoke([&](auto&, bool) -> FilterHeadersStatus {
        encoder_filter->encoder_callbacks_->sendLocalReply(Code::InternalServerError, "", nullptr,
                                                           absl::nullopt, "");
        return FilterHeadersStatus::StopIteration;
      }));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(decoder_filter);
        callbacks.addStreamFilter(encoder_filter);
      }));

  RequestHeaderMapPtr grpc_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"},
                                   {":path", "/"},
                                   {":method", "GET"},
                                   {"content-type", "application/grpc"}}};

  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(absl::make_optional(std::ref(*grpc_headers))));
  filter_manager_->createFilterChain();

  filter_manager_->requestHeadersInitialized();
  EXPECT_CALL(local_reply_, rewrite(_, _, _, _, _, _));
  EXPECT_CALL(filter_manager_callbacks_, setResponseHeaders_(_))
      .WillOnce(Invoke([](auto&) {}))
      .WillOnce(Invoke([](auto& response_headers) {
        EXPECT_THAT(response_headers,
                    HeaderHasValueRef(Http::Headers::get().ContentType, "application/grpc"));
      }));
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, _));
  EXPECT_CALL(filter_manager_callbacks_, endStream());
  filter_manager_->decodeHeaders(*grpc_headers, true);
  filter_manager_->destroyFilters();
}
} // namespace
} // namespace Http
} // namespace Envoy