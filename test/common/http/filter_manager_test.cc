#include "envoy/common/optref.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/matcher/matcher.h"
#include "envoy/stream_info/filter_state.h"

#include "common/http/filter_manager.h"
#include "common/http/matching/inputs.h"
#include "common/matcher/exact_map_matcher.h"
#include "common/matcher/matcher.h"
#include "common/stream_info/filter_state_impl.h"
#include "common/stream_info/stream_info_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_reply/mocks.h"
#include "test/mocks/network/mocks.h"

#include "gtest/gtest.h"

using testing::InSequence;
using testing::Return;

namespace Envoy {
namespace Http {
namespace {
class FilterManagerTest : public testing::Test {
public:
  void initialize() {
    filter_manager_ = std::make_unique<FilterManager>(
        filter_manager_callbacks_, dispatcher_, connection_, 0, nullptr, true, 10000,
        filter_factory_, local_reply_, protocol_, time_source_, filter_state_,
        StreamInfo::FilterState::LifeSpan::Connection);
  }

  std::unique_ptr<FilterManager> filter_manager_;
  NiceMock<MockFilterManagerCallbacks> filter_manager_callbacks_;
  Event::MockDispatcher dispatcher_;
  NiceMock<Network::MockConnection> connection_;
  Envoy::Http::MockFilterChainFactory filter_factory_;
  NiceMock<LocalReply::MockLocalReply> local_reply_;
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
      .WillByDefault(Return(makeOptRef(*grpc_headers)));

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
      .WillByDefault(Return(makeOptRef(*grpc_headers)));
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

struct TestAction : Matcher::ActionBase<ProtobufWkt::StringValue> {};

template <class InputType, class ActionType>
Matcher::MatchTreeSharedPtr<HttpMatchingData> createMatchingTree(const std::string& name,
                                                                 const std::string& value) {
  auto tree = std::make_shared<Matcher::ExactMapMatcher<HttpMatchingData>>(
      std::make_unique<InputType>(name), absl::nullopt);

  tree->addChild(value, Matcher::OnMatch<HttpMatchingData>{
                            []() { return std::make_unique<ActionType>(); }, nullptr});

  return tree;
}

Matcher::MatchTreeSharedPtr<HttpMatchingData> createRequestAndResponseMatchingTree() {
  auto tree = std::make_shared<Matcher::ExactMapMatcher<HttpMatchingData>>(
      std::make_unique<Matching::HttpResponseHeadersDataInput>("match-header"), absl::nullopt);

  tree->addChild("match", Matcher::OnMatch<HttpMatchingData>{
                              []() { return std::make_unique<SkipAction>(); },
                              createMatchingTree<Matching::HttpRequestHeadersDataInput, SkipAction>(
                                  "match-header", "match")});

  return tree;
}

TEST_F(FilterManagerTest, MatchTreeSkipActionDecodingHeaders) {
  initialize();

  // The filter is added, but since we match on the request header we skip the filter.
  std::shared_ptr<MockStreamDecoderFilter> decoder_filter(new MockStreamDecoderFilter());
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*decoder_filter, onDestroy());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(
            decoder_filter, createMatchingTree<Matching::HttpRequestHeadersDataInput, SkipAction>(
                                "match-header", "match"));
      }));

  RequestHeaderMapPtr grpc_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"},
                                   {":path", "/"},
                                   {":method", "GET"},
                                   {"match-header", "match"},
                                   {"content-type", "application/grpc"}}};

  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(makeOptRef(*grpc_headers)));
  filter_manager_->createFilterChain();

  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*grpc_headers, true);
  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, MatchTreeSkipActionRequestAndResponseHeaders) {
  initialize();

  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));

  // This stream filter will skip further callbacks once it sees both the request and response
  // header. As such, it should see the decoding callbacks but none of the encoding callbacks.
  auto stream_filter = std::make_shared<MockStreamFilter>();
  EXPECT_CALL(*stream_filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*stream_filter, onDestroy());
  EXPECT_CALL(*stream_filter, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*stream_filter, decodeData(_, false)).WillOnce(Return(FilterDataStatus::Continue));

  auto decoder_filter = std::make_shared<Envoy::Http::MockStreamDecoderFilter>();
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*decoder_filter, onDestroy());
  EXPECT_CALL(*decoder_filter, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*decoder_filter, decodeData(_, false))
      .WillOnce(Invoke([&](auto&, bool) -> FilterDataStatus {
        ResponseHeaderMapPtr headers{new TestResponseHeaderMapImpl{
            {":status", "200"}, {"match-header", "match"}, {"content-type", "application/grpc"}}};
        decoder_filter->callbacks_->encodeHeaders(std::move(headers), false, "details");

        Buffer::OwnedImpl data("data");
        decoder_filter->callbacks_->encodeData(data, false);

        ResponseTrailerMapPtr trailers{new TestResponseTrailerMapImpl{
            {"some-trailer", "trailer"},
        }};
        decoder_filter->callbacks_->encodeTrailers(std::move(trailers));
        return FilterDataStatus::StopIterationNoBuffer;
      }));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamFilter(stream_filter, createRequestAndResponseMatchingTree());
        callbacks.addStreamDecoderFilter(decoder_filter);
      }));

  RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{{":authority", "host"},
                                                           {":path", "/"},
                                                           {":method", "GET"},
                                                           {"match-header", "match"},
                                                           {"content-type", "application/grpc"}}};
  Buffer::OwnedImpl data("data");

  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return((makeOptRef(*headers))));
  filter_manager_->createFilterChain();

  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, _));
  EXPECT_CALL(filter_manager_callbacks_, endStream());

  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*headers, false);
  filter_manager_->decodeData(data, false);

  RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"trailer", ""}}};
  filter_manager_->decodeTrailers(*trailers);

  filter_manager_->destroyFilters();
}

// Verify that we propagate custom match actions to a decoding filter.
TEST_F(FilterManagerTest, MatchTreeFilterActionDecodingHeaders) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> decoder_filter(new MockStreamDecoderFilter());
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*decoder_filter, onMatchCallback(_));
  EXPECT_CALL(*decoder_filter, decodeHeaders(_, _));
  EXPECT_CALL(*decoder_filter, decodeComplete());
  EXPECT_CALL(*decoder_filter, onDestroy());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(
            decoder_filter, createMatchingTree<Matching::HttpRequestHeadersDataInput, TestAction>(
                                "match-header", "match"));
      }));

  RequestHeaderMapPtr grpc_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"},
                                   {":path", "/"},
                                   {":method", "GET"},
                                   {"match-header", "match"},
                                   {"content-type", "application/grpc"}}};

  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(makeOptRef(*grpc_headers)));
  filter_manager_->createFilterChain();

  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*grpc_headers, true);
  filter_manager_->destroyFilters();
}

// Verify that we propagate custom match actions to a decoding filter when matching on request
// trailers.
TEST_F(FilterManagerTest, MatchTreeFilterActionDecodingTrailers) {
  initialize();
  std::shared_ptr<MockStreamDecoderFilter> decoder_filter(new MockStreamDecoderFilter());
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(
            decoder_filter, createMatchingTree<Matching::HttpRequestTrailersDataInput, TestAction>(
                                "match-trailer", "match"));
      }));

  RequestHeaderMapPtr grpc_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"},
                                   {":path", "/"},
                                   {":method", "GET"},
                                   {"match-header", "match"},
                                   {"content-type", "application/grpc"}}};

  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(makeOptRef(*grpc_headers)));
  filter_manager_->createFilterChain();

  filter_manager_->requestHeadersInitialized();

  EXPECT_CALL(*decoder_filter, decodeHeaders(_, _));
  filter_manager_->decodeHeaders(*grpc_headers, false);

  EXPECT_CALL(*decoder_filter, decodeData(_, _));
  EXPECT_CALL(dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(dispatcher_, popTrackedObject(_));
  Buffer::OwnedImpl empty_buffer;
  filter_manager_->decodeData(empty_buffer, false);

  EXPECT_CALL(*decoder_filter, onMatchCallback(_));
  EXPECT_CALL(*decoder_filter, decodeTrailers(_));
  EXPECT_CALL(*decoder_filter, decodeComplete());
  RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"match-trailer", "match"}}};
  filter_manager_->decodeTrailers(*trailers);

  EXPECT_CALL(*decoder_filter, onDestroy());
  filter_manager_->destroyFilters();
}

// Verify that we propagate custom match actions to an encoding filter when matching on response
// trailers.
TEST_F(FilterManagerTest, MatchTreeFilterActionEncodingTrailers) {
  initialize();
  std::shared_ptr<MockStreamFilter> filter(new MockStreamFilter());
  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamFilter(
            filter, createMatchingTree<Matching::HttpResponseTrailersDataInput, TestAction>(
                        "match-trailer", "match"));
      }));

  EXPECT_CALL(*filter, decodeComplete());
  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([&](auto&, bool) -> FilterHeadersStatus {
        ResponseHeaderMapPtr headers{new TestResponseHeaderMapImpl{
            {":status", "200"}, {"content-type", "application/grpc"}}};
        filter->decoder_callbacks_->encodeHeaders(std::move(headers), false, "details");
        Buffer::OwnedImpl empty_buffer;
        filter->decoder_callbacks_->encodeData(empty_buffer, false);

        ResponseTrailerMapPtr trailers{new TestResponseTrailerMapImpl{{"match-trailer", "match"}}};
        filter->decoder_callbacks_->encodeTrailers(std::move(trailers));

        return FilterHeadersStatus::StopIteration;
      }));

  RequestHeaderMapPtr grpc_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"},
                                   {":path", "/"},
                                   {":method", "GET"},
                                   {"content-type", "application/grpc"}}};

  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(makeOptRef(*grpc_headers)));
  filter_manager_->createFilterChain();

  filter_manager_->requestHeadersInitialized();

  EXPECT_CALL(*filter, onMatchCallback(_));
  EXPECT_CALL(*filter, encodeHeaders(_, _));
  EXPECT_CALL(*filter, encodeData(_, _));
  EXPECT_CALL(*filter, encodeTrailers(_));
  EXPECT_CALL(*filter, encodeComplete());
  filter_manager_->decodeHeaders(*grpc_headers, true);
  EXPECT_CALL(*filter, onDestroy());
  filter_manager_->destroyFilters();
}

// Verify that we propagate custom match actions exactly once to a dual filter.
TEST_F(FilterManagerTest, MatchTreeFilterActionDualFilter) {
  initialize();

  std::shared_ptr<MockStreamFilter> filter(new MockStreamFilter());
  EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*filter, setEncoderFilterCallbacks(_));
  EXPECT_CALL(*filter, decodeComplete());
  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillOnce(Invoke([&](auto&, bool) -> FilterHeadersStatus {
        ResponseHeaderMapPtr headers{new TestResponseHeaderMapImpl{
            {":status", "200"}, {"match-header", "match"}, {"content-type", "application/grpc"}}};
        filter->decoder_callbacks_->encodeHeaders(std::move(headers), true, "details");

        return FilterHeadersStatus::StopIteration;
      }));
  EXPECT_CALL(*filter, onDestroy());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamFilter(
            filter, createMatchingTree<Matching::HttpResponseHeadersDataInput, TestAction>(
                        "match-header", "match"));
      }));

  RequestHeaderMapPtr grpc_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"},
                                   {":path", "/"},
                                   {":method", "GET"},
                                   {"match-header", "match"},
                                   {"content-type", "application/grpc"}}};

  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(makeOptRef(*grpc_headers)));
  filter_manager_->createFilterChain();

  filter_manager_->requestHeadersInitialized();
  EXPECT_CALL(*filter, encodeComplete());
  EXPECT_CALL(*filter, encodeHeaders(_, true));
  EXPECT_CALL(*filter, onMatchCallback(_));
  filter_manager_->decodeHeaders(*grpc_headers, true);
  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, OnLocalReply) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> decoder_filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<MockStreamEncoderFilter> encoder_filter(new NiceMock<MockStreamEncoderFilter>());
  std::shared_ptr<MockStreamFilter> stream_filter(new NiceMock<MockStreamFilter>());

  RequestHeaderMapPtr headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

  ON_CALL(filter_manager_callbacks_, requestHeaders()).WillByDefault(Return(makeOptRef(*headers)));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(decoder_filter);
        callbacks.addStreamFilter(stream_filter);
        callbacks.addStreamEncoderFilter(encoder_filter);
      }));

  filter_manager_->createFilterChain();
  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*headers, true);

  // Make sure all 3 filters get onLocalReply, and that the reset is preserved
  // even if not the last return.
  EXPECT_CALL(*decoder_filter, onLocalReply(_));
  EXPECT_CALL(*stream_filter, onLocalReply(_))
      .WillOnce(Return(LocalErrorStatus::ContinueAndResetStream));
  EXPECT_CALL(*encoder_filter, onLocalReply(_));
  EXPECT_CALL(filter_manager_callbacks_, resetStream());
  decoder_filter->callbacks_->sendLocalReply(Code::InternalServerError, "body", nullptr,
                                             absl::nullopt, "details");

  // The reason for the response (in this case the reset) will still be tracked
  // but as no response is sent the response code will remain absent.
  ASSERT_TRUE(filter_manager_->streamInfo().responseCodeDetails().has_value());
  EXPECT_EQ(filter_manager_->streamInfo().responseCodeDetails().value(), "details");
  EXPECT_FALSE(filter_manager_->streamInfo().responseCode().has_value());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, MultipleOnLocalReply) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> decoder_filter(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<MockStreamEncoderFilter> encoder_filter(new NiceMock<MockStreamEncoderFilter>());
  std::shared_ptr<MockStreamFilter> stream_filter(new NiceMock<MockStreamFilter>());

  RequestHeaderMapPtr headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};

  ON_CALL(filter_manager_callbacks_, requestHeaders()).WillByDefault(Return(makeOptRef(*headers)));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(Invoke([&](FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(decoder_filter);
        callbacks.addStreamFilter(stream_filter);
        callbacks.addStreamEncoderFilter(encoder_filter);
      }));

  filter_manager_->createFilterChain();
  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*headers, true);

  {
    // Set up expectations to be triggered by sendLocalReply at the bottom of
    // this block.
    InSequence s;

    // Make sure all 3 filters get onLocalReply
    EXPECT_CALL(*decoder_filter, onLocalReply(_));
    EXPECT_CALL(*stream_filter, onLocalReply(_));
    EXPECT_CALL(*encoder_filter, onLocalReply(_));

    // Now response encoding begins. Assume a filter co-opts the original reply
    // with a new local reply.
    EXPECT_CALL(*encoder_filter, encodeHeaders(_, _))
        .WillOnce(Invoke([&](ResponseHeaderMap&, bool) -> FilterHeadersStatus {
          decoder_filter->callbacks_->sendLocalReply(Code::InternalServerError, "body2", nullptr,
                                                     absl::nullopt, "details2");
          return FilterHeadersStatus::StopIteration;
        }));

    // All 3 filters should get the second onLocalReply.
    EXPECT_CALL(*decoder_filter, onLocalReply(_));
    EXPECT_CALL(*stream_filter, onLocalReply(_));
    EXPECT_CALL(*encoder_filter, onLocalReply(_));

    decoder_filter->callbacks_->sendLocalReply(Code::InternalServerError, "body", nullptr,
                                               absl::nullopt, "details");
  }

  // The final details should be details2.
  ASSERT_TRUE(filter_manager_->streamInfo().responseCodeDetails().has_value());
  EXPECT_EQ(filter_manager_->streamInfo().responseCodeDetails().value(), "details2");
  EXPECT_FALSE(filter_manager_->streamInfo().responseCode().has_value());

  filter_manager_->destroyFilters();
}

} // namespace
} // namespace Http
} // namespace Envoy
