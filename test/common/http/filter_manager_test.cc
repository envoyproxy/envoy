#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/matcher/matcher.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/http/filter_manager.h"
#include "source/common/http/matching/inputs.h"
#include "source/common/matcher/exact_map_matcher.h"
#include "source/common/matcher/matcher.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_reply/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/overload_manager.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Return;
using testing::StrictMock;

namespace Envoy {
namespace Http {
namespace {
using Protobuf::util::MessageDifferencer;

class FilterManagerTest : public testing::Test {
public:
  void initialize() {
    filter_manager_ = std::make_unique<DownstreamFilterManager>(
        filter_manager_callbacks_, dispatcher_, connection_, 0, nullptr, true, 10000,
        filter_factory_, local_reply_, protocol_, time_source_, filter_state_, overload_manager_);
  }

  // Simple helper to wrapper filter to the factory function.
  FilterFactoryCb createDecoderFilterFactoryCb(StreamDecoderFilterSharedPtr filter) {
    return [filter](FilterChainFactoryCallbacks& callbacks) {
      callbacks.addStreamDecoderFilter(filter);
    };
  }
  FilterFactoryCb createEncoderFilterFactoryCb(StreamEncoderFilterSharedPtr filter) {
    return [filter](FilterChainFactoryCallbacks& callbacks) {
      callbacks.addStreamEncoderFilter(filter);
    };
  }
  FilterFactoryCb createStreamFilterFactoryCb(StreamFilterSharedPtr filter) {
    return [filter](FilterChainFactoryCallbacks& callbacks) { callbacks.addStreamFilter(filter); };
  }
  FilterFactoryCb createLogHandlerFactoryCb(AccessLog::InstanceSharedPtr handler) {
    return [handler](FilterChainFactoryCallbacks& callbacks) {
      callbacks.addAccessLogHandler(handler);
    };
  }

  void validateFilterStateData(const std::string& expected_name) {
    ASSERT_TRUE(filter_manager_->streamInfo().filterState()->hasData<LocalReplyOwnerObject>(
        LocalReplyFilterStateKey));
    auto fs_value =
        filter_manager_->streamInfo().filterState()->getDataReadOnly<LocalReplyOwnerObject>(
            LocalReplyFilterStateKey);
    EXPECT_EQ(fs_value->serializeAsString(), expected_name);

    auto expected = std::make_unique<Protobuf::StringValue>();
    expected->set_value(expected_name);
    EXPECT_TRUE(MessageDifferencer::Equals(*(fs_value->serializeAsProto()), *expected));
  }

  void runSendDirectLocalReplySavedResponseMetadataTest(bool flush_saved_response_metadata) {
    initialize();

    std::shared_ptr<MockStreamFilter> filter_1(new NiceMock<MockStreamFilter>());
    std::shared_ptr<MockStreamFilter> filter_2(new NiceMock<MockStreamFilter>());

    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
          auto factory = createStreamFilterFactoryCb(filter_1);
          callbacks.setFilterConfigName("configName1");
          factory(callbacks);
          factory = createStreamFilterFactoryCb(filter_2);
          callbacks.setFilterConfigName("configName2");
          factory(callbacks);
          return true;
        });

    RequestHeaderMapPtr request_headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    ON_CALL(filter_manager_callbacks_, requestHeaders())
        .WillByDefault(Return(makeOptRef(*request_headers)));
    ON_CALL(filter_manager_callbacks_, responseHeaders())
        .WillByDefault([this]() -> ResponseHeaderMapOptRef {
          return makeOptRefFromPtr(filter_manager_callbacks_.response_headers_.get());
        });

    filter_manager_->createDownstreamFilterChain();
    filter_manager_->requestHeadersInitialized();

    EXPECT_CALL(*filter_1, decodeHeaders(_, _)).WillOnce(Return(FilterHeadersStatus::Continue));
    EXPECT_CALL(*filter_2, decodeHeaders(_, _)).WillOnce(Return(FilterHeadersStatus::Continue));
    filter_manager_->decodeHeaders(*request_headers, false);

    MetadataMap metadata_map = {{"local-reply", "metadata"}};
    EXPECT_CALL(filter_manager_callbacks_, setResponseHeaders_(_)).Times(2);
    EXPECT_CALL(local_reply_, rewrite(_, _, _, _, _, _));
    if (!flush_saved_response_metadata) {
      EXPECT_CALL(filter_manager_callbacks_, encodeMetadata(_)).Times(0);
    }

    {
      InSequence s;
      EXPECT_CALL(*filter_2, encodeHeaders(_, false))
          .WillOnce([&](ResponseHeaderMap&, bool) -> FilterHeadersStatus {
            filter_2->encoder_callbacks_->addEncodedMetadata(
                std::make_unique<MetadataMap>(metadata_map));
            return FilterHeadersStatus::Continue;
          });
      EXPECT_CALL(*filter_1, encodeHeaders(_, false))
          .WillOnce([&](ResponseHeaderMap&, bool) -> FilterHeadersStatus {
            filter_1->encoder_callbacks_->sendLocalReply(Code::InternalServerError, "body", nullptr,
                                                         std::nullopt, "direct_local_reply");
            return FilterHeadersStatus::StopIteration;
          });
      EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
      EXPECT_CALL(filter_manager_callbacks_, encodeData(_, !flush_saved_response_metadata))
          .WillOnce([&](Buffer::Instance& data, bool end_stream) -> void {
            EXPECT_EQ("body", data.toString());
            EXPECT_EQ(!flush_saved_response_metadata, end_stream);
          });
      if (flush_saved_response_metadata) {
        EXPECT_CALL(filter_manager_callbacks_, encodeMetadata(_))
            .WillOnce([&](MetadataMapPtr&& metadata_map_ptr) -> void {
              EXPECT_EQ(metadata_map, *metadata_map_ptr);
            });
        EXPECT_CALL(filter_manager_callbacks_, encodeData(_, true))
            .WillOnce([](Buffer::Instance& data, bool) -> void { EXPECT_EQ(0, data.length()); });
      }
      EXPECT_CALL(filter_manager_callbacks_, endStream());
    }

    filter_2->decoder_callbacks_->encodeHeaders(
        std::make_unique<TestResponseHeaderMapImpl>(TestResponseHeaderMapImpl{{":status", "200"}}),
        false, "upstream_response");

    validateFilterStateData("configName1");

    filter_manager_->destroyFilters();
  }

  void runAddDecodedDataOnContinueTest(bool forward_data);

  // Encoder chain used by the added-trailer tests. Encode order is first_filter,
  // [middle_filter,] last_filter, codec; router_filter is the terminal decoder filter.
  struct EncoderTrailerChain {
    std::shared_ptr<NiceMock<MockStreamEncoderFilter>> last_filter{
        std::make_shared<NiceMock<MockStreamEncoderFilter>>()};
    std::shared_ptr<NiceMock<MockStreamEncoderFilter>> middle_filter;
    std::shared_ptr<NiceMock<MockStreamEncoderFilter>> first_filter{
        std::make_shared<NiceMock<MockStreamEncoderFilter>>()};
    std::shared_ptr<NiceMock<MockStreamDecoderFilter>> router_filter{
        std::make_shared<NiceMock<MockStreamDecoderFilter>>()};
    RequestHeaderMapPtr request_headers;
  };

  // Builds the chain, sends request headers through the router filter, and sends 200 response
  // headers with every encoder filter returning Continue except last_filter, which returns
  // `last_headers_status`, and first_filter, which returns `first_headers_status`.
  EncoderTrailerChain setupEncoderTrailerChain(
      bool with_middle_filter,
      FilterHeadersStatus last_headers_status = FilterHeadersStatus::Continue,
      FilterHeadersStatus first_headers_status = FilterHeadersStatus::Continue);
  void runEncodeDataAddTrailersOnStopIterationNoBufferTest(bool forward_held_trailers);

  std::unique_ptr<DownstreamFilterManager> filter_manager_;
  NiceMock<MockFilterManagerCallbacks> filter_manager_callbacks_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Network::MockConnection> connection_;
  Envoy::Http::MockFilterChainFactory filter_factory_;
  NiceMock<LocalReply::MockLocalReply> local_reply_;
  Protocol protocol_{Protocol::Http2};
  NiceMock<MockTimeSystem> time_source_;
  StreamInfo::FilterStateSharedPtr filter_state_ =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Connection);
  NiceMock<Server::MockOverloadManager> overload_manager_;
};

TEST_F(FilterManagerTest, LowWatermarkWithoutHighWatermark) {
  initialize();

  EXPECT_ENVOY_BUG(
      filter_manager_->callLowWatermarkCallbacks(),
      "HTTP filter manager low watermark callback without a preceding high watermark callback");
  EXPECT_FALSE(filter_manager_->aboveHighWatermark());
  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, RequestHeadersOrResponseHeadersAccess) {
  initialize();

  auto decoder_filter = std::make_shared<NiceMock<MockStreamDecoderFilter>>();
  auto encoder_filter = std::make_shared<NiceMock<MockStreamEncoderFilter>>();

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto decoder_factory = createDecoderFilterFactoryCb(decoder_filter);
        callbacks.setFilterConfigName("");
        decoder_factory(callbacks);
        auto encoder_factory = createEncoderFilterFactoryCb(encoder_filter);
        callbacks.setFilterConfigName("");
        encoder_factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  RequestHeaderMapPtr request_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  RequestTrailerMapPtr request_trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
  ResponseTrailerMapPtr response_trailers{new TestResponseTrailerMapImpl{{"foo", "bar"}}};
  ResponseHeaderMapPtr informational_headers{
      new TestResponseHeaderMapImpl{{":status", "100"}, {"foo", "bar"}}};

  EXPECT_CALL(filter_manager_callbacks_, requestHeaders())
      .Times(2)
      .WillRepeatedly(Return(makeOptRef(*request_headers)));
  EXPECT_CALL(filter_manager_callbacks_, responseHeaders())
      .Times(2)
      .WillRepeatedly(Return(makeOptRef(*response_headers)));
  EXPECT_CALL(filter_manager_callbacks_, requestTrailers())
      .Times(2)
      .WillRepeatedly(Return(makeOptRef(*request_trailers)));
  EXPECT_CALL(filter_manager_callbacks_, responseTrailers())
      .Times(2)
      .WillRepeatedly(Return(makeOptRef(*response_trailers)));
  EXPECT_CALL(filter_manager_callbacks_, informationalHeaders())
      .Times(2)
      .WillRepeatedly(Return(makeOptRef(*informational_headers)));

  EXPECT_EQ(decoder_filter->callbacks_->requestHeaders().ptr(), request_headers.get());
  EXPECT_EQ(decoder_filter->callbacks_->responseHeaders().ptr(), response_headers.get());
  EXPECT_EQ(decoder_filter->callbacks_->requestTrailers().ptr(), request_trailers.get());
  EXPECT_EQ(decoder_filter->callbacks_->responseTrailers().ptr(), response_trailers.get());
  EXPECT_EQ(decoder_filter->callbacks_->informationalHeaders().ptr(), informational_headers.get());

  EXPECT_EQ(encoder_filter->callbacks_->requestHeaders().ptr(), request_headers.get());
  EXPECT_EQ(encoder_filter->callbacks_->responseHeaders().ptr(), response_headers.get());
  EXPECT_EQ(encoder_filter->callbacks_->requestTrailers().ptr(), request_trailers.get());
  EXPECT_EQ(encoder_filter->callbacks_->responseTrailers().ptr(), response_trailers.get());
  EXPECT_EQ(encoder_filter->callbacks_->informationalHeaders().ptr(), informational_headers.get());

  filter_manager_->destroyFilters();
}

// Verifies that the local reply persists the gRPC classification even if the request headers are
// modified.
TEST_F(FilterManagerTest, SendLocalReplyDuringDecodingGrpcClassiciation) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*filter, decodeHeaders(_, true))
      .WillRepeatedly([&](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        headers.setContentType("text/plain");

        filter->callbacks_->sendLocalReply(Code::InternalServerError, "", nullptr, std::nullopt,
                                           "details");

        return FilterHeadersStatus::StopIteration;
      });

  RequestHeaderMapPtr grpc_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"},
                                   {":path", "/"},
                                   {":method", "GET"},
                                   {"content-type", "application/grpc"}}};

  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(makeOptRef(*grpc_headers)));

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        callbacks.setFilterConfigName("configName1");
        factory(callbacks);
        return true;
      });

  filter_manager_->createDownstreamFilterChain();

  filter_manager_->requestHeadersInitialized();

  EXPECT_CALL(local_reply_, rewrite(_, _, _, _, _, _));
  EXPECT_CALL(filter_manager_callbacks_, setResponseHeaders_(_))
      .WillOnce([](auto& response_headers) {
        EXPECT_THAT(response_headers,
                    ContainsHeader(Http::Headers::get().ContentType, "application/grpc"));
      });
  EXPECT_CALL(filter_manager_callbacks_, resetIdleTimer());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, _));
  EXPECT_CALL(filter_manager_callbacks_, endStream());

  filter_manager_->decodeHeaders(*grpc_headers, true);

  validateFilterStateData("configName1");

  filter_manager_->destroyFilters();
}

// Verifies that the local reply persists the gRPC classification even if the request headers are
// modified when directly encoding a response.
TEST_F(FilterManagerTest, SendLocalReplyDuringEncodingGrpcClassiciation) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> decoder_filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(*decoder_filter, decodeHeaders(_, true))
      .WillRepeatedly([&](RequestHeaderMap& headers, bool) -> FilterHeadersStatus {
        headers.setContentType("text/plain");

        ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
        decoder_filter->callbacks_->encodeHeaders(std::move(response_headers), true, "test");

        return FilterHeadersStatus::StopIteration;
      });

  std::shared_ptr<MockStreamFilter> encoder_filter(new NiceMock<MockStreamFilter>());

  EXPECT_CALL(*encoder_filter, encodeHeaders(_, true))
      .WillRepeatedly([&](auto&, bool) -> FilterHeadersStatus {
        encoder_filter->encoder_callbacks_->sendLocalReply(Code::InternalServerError, "", nullptr,
                                                           std::nullopt, "details");
        return FilterHeadersStatus::StopIteration;
      });

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto decoder_factory = createDecoderFilterFactoryCb(decoder_filter);
        callbacks.setFilterConfigName("configName1");
        decoder_factory(callbacks);

        auto stream_factory = createStreamFilterFactoryCb(encoder_filter);
        callbacks.setFilterConfigName("configName2");
        stream_factory(callbacks);
        return true;
      });

  RequestHeaderMapPtr grpc_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"},
                                   {":path", "/"},
                                   {":method", "GET"},
                                   {"content-type", "application/grpc"}}};

  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(makeOptRef(*grpc_headers)));
  filter_manager_->createDownstreamFilterChain();

  filter_manager_->requestHeadersInitialized();
  EXPECT_CALL(local_reply_, rewrite(_, _, _, _, _, _));
  EXPECT_CALL(filter_manager_callbacks_, setResponseHeaders_(_))
      .WillOnce([](auto&) {})
      .WillOnce([](auto& response_headers) {
        EXPECT_THAT(response_headers,
                    ContainsHeader(Http::Headers::get().ContentType, "application/grpc"));
      });
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, _));
  EXPECT_CALL(filter_manager_callbacks_, endStream());

  filter_manager_->decodeHeaders(*grpc_headers, true);

  validateFilterStateData("configName2");

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, SendDirectLocalReplyEncodesSavedResponseMetadata) {
  runSendDirectLocalReplySavedResponseMetadataTest(true);
}

TEST_F(FilterManagerTest, SendDirectLocalReplySkipsSavedResponseMetadataWhenRuntimeGuardDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.direct_local_reply_flush_saved_response_metadata", "false"}});
  runSendDirectLocalReplySavedResponseMetadataTest(false);
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
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto decoder_factory = createDecoderFilterFactoryCb(decoder_filter);
        callbacks.setFilterConfigName("configName1");
        decoder_factory(callbacks);
        auto stream_factory = createStreamFilterFactoryCb(stream_filter);
        callbacks.setFilterConfigName("configName2");
        stream_factory(callbacks);
        auto encoder_factory = createEncoderFilterFactoryCb(encoder_filter);
        callbacks.setFilterConfigName("configName3");
        encoder_factory(callbacks);
        return true;
      });

  filter_manager_->createDownstreamFilterChain();
  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*headers, true);

  // Make sure all 3 filters get onLocalReply, and that the reset is preserved
  // even if not the last return.
  EXPECT_CALL(*decoder_filter, onLocalReply(_))
      .WillOnce(
          [&](const StreamFilterBase::LocalReplyData& local_reply_data) -> Http::LocalErrorStatus {
            EXPECT_THAT(local_reply_data.grpc_status_, testing::Optional(Grpc::Status::Internal));
            EXPECT_EQ(local_reply_data.body_, "body");
            return Http::LocalErrorStatus::Continue;
          });
  EXPECT_CALL(*stream_filter, onLocalReply(_))
      .WillOnce(
          [&](const StreamFilterBase::LocalReplyData& local_reply_data) -> Http::LocalErrorStatus {
            EXPECT_THAT(local_reply_data.grpc_status_, testing::Optional(Grpc::Status::Internal));
            EXPECT_EQ(local_reply_data.body_, "body");
            return LocalErrorStatus::ContinueAndResetStream;
          });
  EXPECT_CALL(*encoder_filter, onLocalReply(_))
      .WillOnce(
          [&](const StreamFilterBase::LocalReplyData& local_reply_data) -> Http::LocalErrorStatus {
            EXPECT_THAT(local_reply_data.grpc_status_, testing::Optional(Grpc::Status::Internal));
            EXPECT_EQ(local_reply_data.body_, "body");
            return Http::LocalErrorStatus::Continue;
          });
  EXPECT_CALL(filter_manager_callbacks_, resetStream(_, _));
  decoder_filter->callbacks_->sendLocalReply(Code::InternalServerError, "body", nullptr,
                                             Grpc::Status::Internal, "details");

  // The reason for the response (in this case the reset) will still be tracked
  // but as no response is sent the response code will remain absent.

  ASSERT_TRUE(filter_manager_->streamInfo().responseCodeDetails().has_value());
  EXPECT_EQ(filter_manager_->streamInfo().responseCodeDetails().value(), "details");
  EXPECT_FALSE(filter_manager_->streamInfo().responseCode().has_value());

  validateFilterStateData("configName1");

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
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto decoder_factory = createDecoderFilterFactoryCb(decoder_filter);
        callbacks.setFilterConfigName("configName1");
        decoder_factory(callbacks);
        auto stream_factory = createStreamFilterFactoryCb(stream_filter);
        callbacks.setFilterConfigName("configName2");
        stream_factory(callbacks);
        auto encoder_factory = createEncoderFilterFactoryCb(encoder_filter);
        callbacks.setFilterConfigName("configName3");
        encoder_factory(callbacks);
        return true;
      });

  filter_manager_->createDownstreamFilterChain();
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
        .WillOnce([&](ResponseHeaderMap&, bool) -> FilterHeadersStatus {
          decoder_filter->callbacks_->sendLocalReply(Code::InternalServerError, "body2", nullptr,
                                                     std::nullopt, "details2");
          return FilterHeadersStatus::StopIteration;
        });

    // All 3 filters should get the second onLocalReply.
    EXPECT_CALL(*decoder_filter, onLocalReply(_));
    EXPECT_CALL(*stream_filter, onLocalReply(_));
    EXPECT_CALL(*encoder_filter, onLocalReply(_));
    // trackedObjectStackIsEmpty() is never called since sendLocalReply will abort encoder filter
    // iteration.
    EXPECT_CALL(dispatcher_, trackedObjectStackIsEmpty()).Times(0);

    decoder_filter->callbacks_->sendLocalReply(Code::InternalServerError, "body", nullptr,
                                               std::nullopt, "details");
  }

  // The final details should be details2.
  ASSERT_TRUE(filter_manager_->streamInfo().responseCodeDetails().has_value());
  EXPECT_EQ(filter_manager_->streamInfo().responseCodeDetails().value(), "details2");
  EXPECT_FALSE(filter_manager_->streamInfo().responseCode().has_value());

  validateFilterStateData("configName1");

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, ResetIdleTimer) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> decoder_filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto decoder_factory = createDecoderFilterFactoryCb(decoder_filter);
        callbacks.setFilterConfigName("");
        decoder_factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  EXPECT_CALL(filter_manager_callbacks_, resetIdleTimer());
  decoder_filter->callbacks_->resetIdleTimer();

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, SetAndGetUpstreamOverrideHost) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> decoder_filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto decoder_factory = createDecoderFilterFactoryCb(decoder_filter);
        callbacks.setFilterConfigName("");
        decoder_factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  decoder_filter->callbacks_->setUpstreamOverrideHost(
      Upstream::LoadBalancerContext::OverrideHost{"1.2.3.4", true});

  OptRef<const Upstream::LoadBalancerContext::OverrideHost> override_host =
      decoder_filter->callbacks_->upstreamOverrideHost();
  EXPECT_TRUE(override_host.has_value());
  EXPECT_EQ(override_host->host, "1.2.3.4");
  EXPECT_TRUE(override_host->strict);

  filter_manager_->destroyFilters();
};

TEST_F(FilterManagerTest, GetRouteLevelFilterConfig) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> decoder_filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto decoder_factory = createDecoderFilterFactoryCb(decoder_filter);
        callbacks.setFilterConfigName("custom-name");
        decoder_factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  std::shared_ptr<Router::MockRoute> route(new NiceMock<Router::MockRoute>());
  auto route_config = std::make_shared<Router::RouteSpecificFilterConfig>();

  NiceMock<MockDownstreamStreamFilterCallbacks> downstream_callbacks;
  ON_CALL(filter_manager_callbacks_, downstreamCallbacks)
      .WillByDefault(Return(OptRef<DownstreamStreamFilterCallbacks>{downstream_callbacks}));
  ON_CALL(downstream_callbacks, route(_))
      .WillByDefault(Return(makeOptRefFromPtr<const Router::Route>(route.get())));

  // Get a valid config by the custom filter name.
  EXPECT_CALL(*route, mostSpecificPerFilterConfig(testing::Eq("custom-name")))
      .WillOnce(Return(route_config.get()));
  EXPECT_EQ(route_config.get(), decoder_filter->callbacks_->mostSpecificPerFilterConfig());

  // Get nothing by the custom filter name.
  EXPECT_CALL(*route, mostSpecificPerFilterConfig(testing::Eq("custom-name")))
      .WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, decoder_filter->callbacks_->mostSpecificPerFilterConfig());

  // Get a valid config by the custom filter name.
  EXPECT_CALL(*route, perFilterConfigs(testing::Eq("custom-name")))
      .WillOnce([&](absl::string_view) -> Router::RouteSpecificFilterConfigs {
        return {route_config.get()};
      });
  auto route_config_result = decoder_filter->callbacks_->perFilterConfigs();
  EXPECT_EQ(route_config.get(), route_config_result[0]);

  // Get nothing by the custom filter name.
  EXPECT_CALL(*route, perFilterConfigs(testing::Eq("custom-name")))
      .WillOnce([](absl::string_view) -> Router::RouteSpecificFilterConfigs { return {}; });
  route_config_result = decoder_filter->callbacks_->perFilterConfigs();
  EXPECT_TRUE(route_config_result.empty());

  filter_manager_->destroyFilters();
};

TEST_F(FilterManagerTest, GetRouteLevelFilterConfigForNullRoute) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> decoder_filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto decoder_factory = createDecoderFilterFactoryCb(decoder_filter);
        callbacks.setFilterConfigName("custom-name");
        decoder_factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  std::shared_ptr<Router::MockRoute> route(new NiceMock<Router::MockRoute>());
  auto route_config = std::make_shared<Router::RouteSpecificFilterConfig>();

  // Do nothing for no route.
  NiceMock<MockDownstreamStreamFilterCallbacks> downstream_callbacks;
  ON_CALL(filter_manager_callbacks_, downstreamCallbacks)
      .WillByDefault(Return(OptRef<DownstreamStreamFilterCallbacks>{downstream_callbacks}));
  EXPECT_CALL(downstream_callbacks, route(_)).WillOnce(Return(OptRef<const Router::Route>{}));
  decoder_filter->callbacks_->mostSpecificPerFilterConfig();

  EXPECT_CALL(downstream_callbacks, route(_)).WillOnce(Return(OptRef<const Router::Route>{}));
  decoder_filter->callbacks_->perFilterConfigs();

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, MetadataContinueAll) {
  initialize();

  std::shared_ptr<MockStreamFilter> filter_1(new NiceMock<MockStreamFilter>());

  std::shared_ptr<MockStreamFilter> filter_2(new NiceMock<MockStreamFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto decoder_factory = createStreamFilterFactoryCb(filter_1);
        callbacks.setFilterConfigName("configName1");
        decoder_factory(callbacks);
        decoder_factory = createStreamFilterFactoryCb(filter_2);
        callbacks.setFilterConfigName("configName2");
        decoder_factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  // Decode path:
  EXPECT_CALL(*filter_1, decodeHeaders(_, _)).WillOnce(Return(FilterHeadersStatus::StopIteration));
  RequestHeaderMapPtr basic_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(makeOptRef(*basic_headers)));

  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*basic_headers, false);

  EXPECT_CALL(*filter_1, decodeMetadata(_)).WillOnce(Return(FilterMetadataStatus::ContinueAll));
  MetadataMap map1 = {{"a", "b"}};
  MetadataMap map2 = {{"c", "d"}};
  EXPECT_CALL(*filter_2, decodeHeaders(_, _)).WillOnce([&]() {
    filter_2->decoder_callbacks_->addDecodedMetadata().push_back(
        std::make_unique<MetadataMap>(map2));
    return FilterHeadersStatus::Continue;
  });
  {
    InSequence s;
    // Metadata added by filter_2.decodeHeaders(..) appears second, and goes through all filters.
    EXPECT_CALL(*filter_2, decodeMetadata(testing::Eq(map1)));
    EXPECT_CALL(*filter_1, decodeMetadata(testing::Eq(map2)));
    EXPECT_CALL(*filter_2, decodeMetadata(testing::Eq(map2)));
  }
  filter_manager_->decodeMetadata(map1);

  // Encode Path:
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  ON_CALL(filter_manager_callbacks_, responseHeaders())
      .WillByDefault(Return(makeOptRef(*response_headers)));

  EXPECT_CALL(*filter_2, encodeHeaders(_, _)).WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*filter_1, encodeHeaders(_, _)).WillOnce([&]() {
    filter_2->encoder_callbacks_->addEncodedMetadata(std::make_unique<MetadataMap>(map2));
    return FilterHeadersStatus::Continue;
  });
  EXPECT_CALL(*filter_2, encodeMetadata(_)).WillOnce(Return(FilterMetadataStatus::ContinueAll));
  {
    InSequence s;
    // Metadata added by filter_1.decodeHeaders(..) appears second.
    EXPECT_CALL(*filter_1, encodeMetadata(testing::Eq(map1)));
    // The encode path for metadata is different (by design or coincidence) than the decode path.
    // On the encode path, calling addEncodedMetadata will only pass through this and later filters,
    // while on the decode path, calling addDecodedMetadata will pass metadata through all filters.
    EXPECT_CALL(*filter_2, encodeMetadata(testing::Eq(map2))).Times(0);
    EXPECT_CALL(*filter_1, encodeMetadata(testing::Eq(map2)));
  }

  filter_2->decoder_callbacks_->encodeHeaders(
      std::make_unique<TestResponseHeaderMapImpl>(*response_headers), false, "none");
  filter_2->decoder_callbacks_->encodeMetadata(std::make_unique<MetadataMap>(map1));

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, DecodeMetadataSendsLocalReply) {
  initialize();

  std::shared_ptr<MockStreamFilter> filter_1(new NiceMock<MockStreamFilter>());

  std::shared_ptr<MockStreamFilter> filter_2(new NiceMock<MockStreamFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createStreamFilterFactoryCb(filter_1);
        callbacks.setFilterConfigName("configName1");
        factory(callbacks);
        factory = createStreamFilterFactoryCb(filter_2);
        callbacks.setFilterConfigName("configName2");
        factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  RequestHeaderMapPtr basic_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(makeOptRef(*basic_headers)));

  EXPECT_CALL(*filter_1, decodeHeaders(_, _));
  EXPECT_CALL(*filter_2, decodeHeaders(_, _));
  filter_manager_->decodeHeaders(*basic_headers, false);

  EXPECT_CALL(*filter_1, decodeMetadata(_)).WillOnce([&]() {
    filter_1->decoder_callbacks_->sendLocalReply(Code::InternalServerError, "bad_metadata", nullptr,
                                                 std::nullopt, "bad_metadata");
    return FilterMetadataStatus::StopIterationForLocalReply;
  });

  // Metadata does not pass to the next filter due to local reply.
  EXPECT_CALL(*filter_2, decodeMetadata(_)).Times(0);
  // Triggered local reply should pass through encode headers for both filters.
  EXPECT_CALL(*filter_2, encodeHeaders(_, _));
  EXPECT_CALL(*filter_1, encodeHeaders(_, _));
  MetadataMap map = {{"a", "b"}};
  filter_manager_->decodeMetadata(map);

  EXPECT_THAT(*filter_manager_->streamInfo().responseCodeDetails(), "bad_metadata");

  validateFilterStateData("configName1");

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, MetadataContinueAllFollowedByHeadersLocalReply) {
  initialize();

  std::shared_ptr<MockStreamFilter> filter_1(new NiceMock<MockStreamFilter>());

  std::shared_ptr<MockStreamFilter> filter_2(new NiceMock<MockStreamFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto decoder_factory = createStreamFilterFactoryCb(filter_1);
        callbacks.setFilterConfigName("configName1");
        decoder_factory(callbacks);
        decoder_factory = createStreamFilterFactoryCb(filter_2);
        callbacks.setFilterConfigName("configName2");
        decoder_factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  // Decode path:
  EXPECT_CALL(*filter_1, decodeHeaders(_, _)).WillOnce(Return(FilterHeadersStatus::StopIteration));
  RequestHeaderMapPtr basic_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(makeOptRef(*basic_headers)));

  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*basic_headers, false);

  EXPECT_CALL(*filter_1, decodeMetadata(_)).WillOnce(Return(FilterMetadataStatus::ContinueAll));
  MetadataMap map1 = {{"a", "b"}};
  MetadataMap map2 = {{"c", "d"}};
  EXPECT_CALL(*filter_2, decodeHeaders(_, _)).WillOnce([&]() {
    filter_2->decoder_callbacks_->sendLocalReply(Code::InternalServerError, "bad_headers", nullptr,
                                                 std::nullopt, "bad_headers");
    return FilterHeadersStatus::StopIteration;
  });
  // filter_2 should never decode metadata.
  EXPECT_CALL(*filter_2, decodeMetadata(_)).Times(0);
  filter_manager_->decodeMetadata(map1);
  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, EncodeMetadataSendsLocalReply) {
  initialize();

  std::shared_ptr<MockStreamFilter> filter_1(new NiceMock<MockStreamFilter>());

  std::shared_ptr<MockStreamFilter> filter_2(new NiceMock<MockStreamFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createStreamFilterFactoryCb(filter_1);
        callbacks.setFilterConfigName("configName1");
        factory(callbacks);
        factory = createStreamFilterFactoryCb(filter_2);
        callbacks.setFilterConfigName("configName2");
        factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  // Encode headers first, as metadata can't get ahead of headers.
  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  ON_CALL(filter_manager_callbacks_, responseHeaders())
      .WillByDefault(Return(makeOptRef(*response_headers)));

  EXPECT_CALL(*filter_2, encodeHeaders(_, _));
  EXPECT_CALL(*filter_1, encodeHeaders(_, _));
  filter_2->decoder_callbacks_->encodeHeaders(
      std::make_unique<TestResponseHeaderMapImpl>(*response_headers), false, "");

  EXPECT_CALL(*filter_2, encodeMetadata(_)).WillOnce([&]() {
    filter_2->encoder_callbacks_->sendLocalReply(Code::InternalServerError, "", nullptr,
                                                 std::nullopt, "bad_metadata");
    return FilterMetadataStatus::StopIterationForLocalReply;
  });
  // Headers have already passed through; we will reset the stream.
  EXPECT_CALL(filter_manager_callbacks_, resetStream(StreamResetReason::LocalReset, ""));
  MetadataMap map1 = {{"a", "b"}};
  filter_2->decoder_callbacks_->encodeMetadata(std::make_unique<MetadataMap>(map1));

  validateFilterStateData("configName2");

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, RequestedApplicationProtocols) {
  initialize();

  const auto& protocols =
      filter_manager_->streamInfo().downstreamAddressProvider().requestedApplicationProtocols();
  EXPECT_TRUE(protocols.empty());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, IdleTimerResets) {
  initialize();

  std::shared_ptr<MockStreamFilter> filter_1(new NiceMock<MockStreamFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createStreamFilterFactoryCb(filter_1);
        callbacks.setFilterConfigName("configName1");
        factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  RequestHeaderMapPtr basic_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(makeOptRef(*basic_headers)));
  filter_manager_->requestHeadersInitialized();

  filter_manager_->decodeHeaders(*basic_headers, false);

  Buffer::OwnedImpl data("absee");
  EXPECT_CALL(filter_manager_callbacks_, resetIdleTimer());
  filter_manager_->decodeData(data, false);

  MetadataMap map = {{"a", "b"}};
  EXPECT_CALL(filter_manager_callbacks_, resetIdleTimer());
  filter_manager_->decodeMetadata(map);

  RequestTrailerMapPtr basic_trailers{new TestRequestTrailerMapImpl{{"x", "y"}}};
  filter_manager_->decodeTrailers(*basic_trailers);

  ResponseHeaderMapPtr response_headers{new TestResponseHeaderMapImpl{{":status", "200"}}};
  ON_CALL(filter_manager_callbacks_, responseHeaders())
      .WillByDefault(Return(makeOptRef(*response_headers)));

  EXPECT_CALL(filter_manager_callbacks_, resetIdleTimer());
  filter_1->decoder_callbacks_->encodeHeaders(
      std::make_unique<TestResponseHeaderMapImpl>(*response_headers), false, "");

  EXPECT_CALL(filter_manager_callbacks_, resetIdleTimer());
  filter_1->decoder_callbacks_->encodeData(data, false);

  EXPECT_CALL(filter_manager_callbacks_, resetIdleTimer());
  filter_1->decoder_callbacks_->encodeMetadata(std::make_unique<MetadataMap>(map));

  ResponseTrailerMapPtr basic_resp_trailers{new TestResponseTrailerMapImpl{{"x", "y"}}};
  EXPECT_CALL(filter_manager_callbacks_, resetIdleTimer());
  filter_1->decoder_callbacks_->encodeTrailers(std::move(basic_resp_trailers));
  filter_manager_->destroyFilters();
}

// Verify that decodeData is not called on filters after the stream has been reset.
TEST_F(FilterManagerTest, DecodeDataNotCalledAfterDownstreamReset) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        callbacks.setFilterConfigName("test_filter");
        factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  RequestHeaderMapPtr headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders()).WillByDefault(Return(makeOptRef(*headers)));

  EXPECT_CALL(*filter, decodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*headers, false);

  // Simulate a downstream reset.
  filter_manager_->onDownstreamReset();

  // After reset, decodeData should not be called on the filter.
  EXPECT_CALL(*filter, decodeData(_, _)).Times(0);

  Buffer::OwnedImpl data("test_data");
  filter_manager_->decodeData(data, false);

  filter_manager_->destroyFilters();
}

// Verify that decodeHeaders is not called on filters after the stream has been reset.
TEST_F(FilterManagerTest, DecodeHeadersNotCalledAfterDownstreamReset) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        callbacks.setFilterConfigName("test_filter");
        factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  // Simulate a downstream reset before headers are processed.
  filter_manager_->onDownstreamReset();

  // After reset, decodeHeaders should not be called on the filter.
  EXPECT_CALL(*filter, decodeHeaders(_, _)).Times(0);

  RequestHeaderMapPtr headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders()).WillByDefault(Return(makeOptRef(*headers)));

  filter_manager_->decodeHeaders(*headers, true);

  filter_manager_->destroyFilters();
}

// Verify that decodeTrailers is not called on filters after the stream has been reset.
TEST_F(FilterManagerTest, DecodeTrailersNotCalledAfterDownstreamReset) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        callbacks.setFilterConfigName("test_filter");
        factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  RequestHeaderMapPtr headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders()).WillByDefault(Return(makeOptRef(*headers)));

  EXPECT_CALL(*filter, decodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*headers, false);

  // Simulate a downstream reset.
  filter_manager_->onDownstreamReset();

  // After reset, decodeTrailers should not be called on the filter.
  EXPECT_CALL(*filter, decodeTrailers(_)).Times(0);

  RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
  ON_CALL(filter_manager_callbacks_, requestTrailers())
      .WillByDefault(Return(makeOptRef(*trailers)));

  filter_manager_->decodeTrailers(*trailers);

  filter_manager_->destroyFilters();
}

// Verify that decodeMetadata is not called on filters after the stream has been reset.
TEST_F(FilterManagerTest, DecodeMetadataNotCalledAfterDownstreamReset) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        callbacks.setFilterConfigName("test_filter");
        factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  RequestHeaderMapPtr headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders()).WillByDefault(Return(makeOptRef(*headers)));

  EXPECT_CALL(*filter, decodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*headers, false);

  // Simulate a downstream reset.
  filter_manager_->onDownstreamReset();

  // After reset, decodeMetadata should not be called on the filter.
  EXPECT_CALL(*filter, decodeMetadata(_)).Times(0);

  MetadataMap metadata_map{{"key", "value"}};
  filter_manager_->decodeMetadata(metadata_map);

  filter_manager_->destroyFilters();
}

// Verify that multiple decode operations are all blocked after downstream reset.
TEST_F(FilterManagerTest, AllDecodeOperationsBlockedAfterDownstreamReset) {
  initialize();

  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        callbacks.setFilterConfigName("test_filter");
        factory(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  RequestHeaderMapPtr headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders()).WillByDefault(Return(makeOptRef(*headers)));

  EXPECT_CALL(*filter, decodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*headers, false);

  // Simulate a downstream reset.
  filter_manager_->onDownstreamReset();

  // After reset, none of the decode operations should call the filter.
  EXPECT_CALL(*filter, decodeData(_, _)).Times(0);
  EXPECT_CALL(*filter, decodeTrailers(_)).Times(0);
  EXPECT_CALL(*filter, decodeMetadata(_)).Times(0);

  // Try all decode operations. None of them should reach the filter.
  Buffer::OwnedImpl data("test_data");
  filter_manager_->decodeData(data, false);

  MetadataMap metadata_map{{"key", "value"}};
  filter_manager_->decodeMetadata(metadata_map);

  RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
  ON_CALL(filter_manager_callbacks_, requestTrailers())
      .WillByDefault(Return(makeOptRef(*trailers)));
  filter_manager_->decodeTrailers(*trailers);

  filter_manager_->destroyFilters();
}

// Reproduces the request-body frame loss reported in
// https://github.com/Kuadrant/wasm-shim/issues/388 (and Envoy #46841): a filter
// that returns StopIteration on headers (e.g. a wasm filter with
// allow_on_headers_stop_iteration, mapped to IterationState::StopSingleIteration)
// buffers a body chunk, continues asynchronously, and then on the next chunk
// drains the frame into the filter-manager buffer via addDecodedData() while
// returning Continue. Because the filter's IterationState is already Continue at
// that point, commonHandleAfterDataCallback() forwards the now-empty frame
// instead of the just-buffered data, so a downstream FULL_DUPLEX_STREAMED
// ext_proc filter never receives that chunk. Exactly one mid-stream frame is
// lost while the message count is unchanged.
//
// The `forward_data` parameter toggles the
// `filter_manager_forward_added_data_on_continue` runtime guard: when disabled
// this documents the pre-fix data loss; when enabled it verifies the fix.
void FilterManagerTest::runAddDecodedDataOnContinueTest(bool forward_data) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.filter_manager_forward_added_data_on_continue",
        forward_data ? "true" : "false"}});

  initialize();

  // filter_1 mimics the wasm filter (stops on headers, buffers, continues after
  // an async callout); filter_2 mimics a FULL_DUPLEX_STREAMED ext_proc filter
  // that streams (does not re-buffer) every request body byte it receives.
  std::shared_ptr<MockStreamDecoderFilter> filter_1(new NiceMock<MockStreamDecoderFilter>());
  std::shared_ptr<MockStreamDecoderFilter> filter_2(new NiceMock<MockStreamDecoderFilter>());

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter_1);
        callbacks.setFilterConfigName("filter_1");
        factory(callbacks);
        factory = createDecoderFilterFactoryCb(filter_2);
        callbacks.setFilterConfigName("filter_2");
        factory(callbacks);
        return true;
      });

  RequestHeaderMapPtr headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders()).WillByDefault(Return(makeOptRef(*headers)));

  filter_manager_->createDownstreamFilterChain();
  filter_manager_->requestHeadersInitialized();

  // filter_2 accumulates and drains (streams) every request body byte it sees.
  std::string filter_2_received;
  ON_CALL(*filter_2, decodeData(_, _))
      .WillByDefault([&](Buffer::Instance& data, bool) -> FilterDataStatus {
        filter_2_received += data.toString();
        data.drain(data.length());
        return FilterDataStatus::StopIterationNoBuffer;
      });

  // filter_1 stops iteration on headers to await an async callout; filter_2 must
  // not see the headers yet.
  EXPECT_CALL(*filter_1, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  filter_manager_->decodeHeaders(*headers, false);

  // Chunk A arrives while filter_1 is paused: buffer it in the filter manager.
  EXPECT_CALL(*filter_1, decodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  Buffer::OwnedImpl chunk_a("AAAA");
  filter_manager_->decodeData(chunk_a, false);

  // The async callout completes: filter_1 continues, which delivers the buffered
  // chunk A (and the headers) down to filter_2.
  EXPECT_CALL(*filter_2, decodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  filter_1->callbacks_->continueDecoding();
  EXPECT_EQ("AAAA", filter_2_received);

  // Chunk B arrives right after the continue. filter_1, still in its buffering
  // mode, moves the frame into the filter-manager buffer via addDecodedData()
  // and then returns Continue. This frame must still reach filter_2.
  EXPECT_CALL(*filter_1, decodeData(_, false))
      .WillOnce([&](Buffer::Instance& data, bool) -> FilterDataStatus {
        filter_1->callbacks_->addDecodedData(data, false);
        return FilterDataStatus::Continue;
      });
  Buffer::OwnedImpl chunk_b("BBBB");
  filter_manager_->decodeData(chunk_b, false);

  // Chunk C (end of stream): filter_1 passes it straight through.
  EXPECT_CALL(*filter_1, decodeData(_, true)).WillOnce(Return(FilterDataStatus::Continue));
  Buffer::OwnedImpl chunk_c("CCCC");
  filter_manager_->decodeData(chunk_c, true);

  // With the fix (guard enabled) all three frames reach filter_2. Without it,
  // chunk B is dropped and filter_2 only sees "AAAACCCC".
  if (forward_data) {
    EXPECT_EQ("AAAABBBBCCCC", filter_2_received);
  } else {
    EXPECT_EQ("AAAACCCC", filter_2_received);
  }

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, DecodeDataFrameLostAfterContinueWithoutGuard) {
  runAddDecodedDataOnContinueTest(/*forward_data=*/false);
}

TEST_F(FilterManagerTest, DecodeDataFrameNotLostAfterContinueWithAddDecodedData) {
  runAddDecodedDataOnContinueTest(/*forward_data=*/true);
}

class FilterManagerPerFilterBufferTest : public FilterManagerTest {
public:
  using Chunks = std::vector<std::pair<std::string, bool>>;

  struct FilterRecord {
    std::function<FilterHeadersStatus(bool)> decode_headers = [](bool) {
      return FilterHeadersStatus::Continue;
    };
    std::function<FilterDataStatus(Buffer::Instance&, bool)> decode_data =
        [](Buffer::Instance&, bool) { return FilterDataStatus::Continue; };
    std::function<FilterTrailersStatus()> decode_trailers = [] {
      return FilterTrailersStatus::Continue;
    };
    std::function<FilterHeadersStatus(bool)> encode_headers = [](bool) {
      return FilterHeadersStatus::Continue;
    };
    std::function<FilterDataStatus(Buffer::Instance&, bool)> encode_data =
        [](Buffer::Instance&, bool) { return FilterDataStatus::Continue; };
    std::function<FilterTrailersStatus()> encode_trailers = [] {
      return FilterTrailersStatus::Continue;
    };
    std::vector<bool> decoded_headers;
    Chunks decoded_data;
    int decoded_trailers{};
    std::vector<bool> encoded_headers;
    Chunks encoded_data;
    int encoded_trailers{};
  };

  void initializeChain(bool per_filter_buffer) {
    scoped_runtime_.mergeValues({{"envoy.reloadable_features.filter_manager_per_filter_buffer",
                                  per_filter_buffer ? "true" : "false"}});
    initialize();

    // The last filter consumes the request body, as the router does.
    records_[2].decode_data = [](Buffer::Instance& data, bool) {
      data.drain(data.length());
      return FilterDataStatus::Continue;
    };
    for (size_t i = 0; i < filters_.size(); i++) {
      filters_[i] = std::make_shared<NiceMock<MockStreamFilter>>();
      FilterRecord& record = records_[i];
      ON_CALL(*filters_[i], decodeHeaders(_, _))
          .WillByDefault(Invoke([&record](RequestHeaderMap&, bool end_stream) {
            record.decoded_headers.push_back(end_stream);
            return record.decode_headers(end_stream);
          }));
      ON_CALL(*filters_[i], decodeData(_, _))
          .WillByDefault(Invoke([&record](Buffer::Instance& data, bool end_stream) {
            record.decoded_data.emplace_back(data.toString(), end_stream);
            return record.decode_data(data, end_stream);
          }));
      ON_CALL(*filters_[i], decodeTrailers(_)).WillByDefault(Invoke([&record](RequestTrailerMap&) {
        record.decoded_trailers++;
        return record.decode_trailers();
      }));
      ON_CALL(*filters_[i], encodeHeaders(_, _))
          .WillByDefault(Invoke([&record](ResponseHeaderMap&, bool end_stream) {
            record.encoded_headers.push_back(end_stream);
            return record.encode_headers(end_stream);
          }));
      ON_CALL(*filters_[i], encodeData(_, _))
          .WillByDefault(Invoke([&record](Buffer::Instance& data, bool end_stream) {
            record.encoded_data.emplace_back(data.toString(), end_stream);
            return record.encode_data(data, end_stream);
          }));
      ON_CALL(*filters_[i], encodeTrailers(_)).WillByDefault(Invoke([&record](ResponseTrailerMap&) {
        record.encoded_trailers++;
        return record.encode_trailers();
      }));
    }

    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillRepeatedly(Invoke([this](FilterChainFactoryCallbacks& callbacks) -> bool {
          // The filter manager keeps a view of the name, so it must outlive the stream.
          constexpr std::array<absl::string_view, 3> names{"filter0", "filter1", "filter2"};
          for (size_t i = 0; i < filters_.size(); i++) {
            callbacks.setFilterConfigName(names[i]);
            createStreamFilterFactoryCb(filters_[i])(callbacks);
          }
          return true;
        }));

    ON_CALL(filter_manager_callbacks_, requestHeaders())
        .WillByDefault(Return(makeOptRef(*request_headers_)));
    ON_CALL(filter_manager_callbacks_, requestTrailers())
        .WillByDefault(Invoke([this]() -> RequestTrailerMapOptRef {
          return makeOptRefFromPtr(request_trailers_.get());
        }));
    ON_CALL(filter_manager_callbacks_, setRequestTrailers(_))
        .WillByDefault(Invoke(
            [this](RequestTrailerMapPtr&& trailers) { request_trailers_ = std::move(trailers); }));
    ON_CALL(filter_manager_callbacks_, encodeHeaders(_, _))
        .WillByDefault(Invoke(
            [this](ResponseHeaderMap&, bool end_stream) { sent_headers_.push_back(end_stream); }));
    ON_CALL(filter_manager_callbacks_, encodeData(_, _))
        .WillByDefault(Invoke([this](Buffer::Instance& data, bool end_stream) {
          sent_data_.emplace_back(data.toString(), end_stream);
          data.drain(data.length());
        }));
    ON_CALL(filter_manager_callbacks_, encodeTrailers(_))
        .WillByDefault(Invoke([this](ResponseTrailerMap&) { sent_trailers_++; }));

    filter_manager_->createDownstreamFilterChain();
    filter_manager_->requestHeadersInitialized();
  }

  void decodeData(absl::string_view body, bool end_stream) {
    Buffer::OwnedImpl data(body);
    filter_manager_->decodeData(data, end_stream);
  }

  void decodeTrailers() {
    request_trailers_ = std::make_unique<TestRequestTrailerMapImpl>(
        TestRequestTrailerMapImpl{{"trailer", "value"}});
    filter_manager_->decodeTrailers(*request_trailers_);
  }

  // The response is originated by the last decoder filter, i.e. the first encoder filter.
  void encodeHeaders(bool end_stream) {
    filters_[2]->decoder_callbacks_->encodeHeaders(
        std::make_unique<TestResponseHeaderMapImpl>(TestResponseHeaderMapImpl{{":status", "200"}}),
        end_stream, "details");
  }

  void encodeData(absl::string_view body, bool end_stream) {
    Buffer::OwnedImpl data(body);
    filters_[2]->decoder_callbacks_->encodeData(data, end_stream);
  }

  void encodeTrailers() {
    filters_[2]->decoder_callbacks_->encodeTrailers(std::make_unique<TestResponseTrailerMapImpl>(
        TestResponseTrailerMapImpl{{"trailer", "value"}}));
  }

  static std::string concat(const Chunks& chunks) {
    std::string result;
    for (const auto& chunk : chunks) {
      result += chunk.first;
    }
    return result;
  }

  // Request body "aaa" is buffered at filter 1 and the final "bbb" at filter 0.
  void bufferRequestBodyAtTwoFilters(FilterDataStatus buffer_status) {
    records_[0].decode_data = [buffer_status](Buffer::Instance&, bool end_stream) {
      return end_stream ? buffer_status : FilterDataStatus::Continue;
    };
    records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
    records_[1].decode_data = [buffer_status](Buffer::Instance&, bool end_stream) {
      return end_stream ? FilterDataStatus::Continue : buffer_status;
    };

    filter_manager_->decodeHeaders(*request_headers_, false);
    decodeData("aaa", false);
    decodeData("bbb", true);
  }

  // Response body "aaa" is buffered at filter 1 and the final "bbb" at filter 2, which is the
  // first encoder filter.
  void bufferResponseBodyAtTwoFilters() {
    filter_manager_->decodeHeaders(*request_headers_, true);
    records_[2].encode_data = [](Buffer::Instance&, bool end_stream) {
      return end_stream ? FilterDataStatus::StopIterationAndBuffer : FilterDataStatus::Continue;
    };
    records_[1].encode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
    records_[1].encode_data = [](Buffer::Instance&, bool end_stream) {
      return end_stream ? FilterDataStatus::Continue : FilterDataStatus::StopIterationAndBuffer;
    };

    encodeHeaders(false);
    encodeData("aaa", false);
    encodeData("bbb", true);
  }

  TestScopedRuntime scoped_runtime_;
  std::array<std::shared_ptr<MockStreamFilter>, 3> filters_;
  std::array<FilterRecord, 3> records_;
  RequestHeaderMapPtr request_headers_{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
  RequestTrailerMapPtr request_trailers_;
  std::vector<bool> sent_headers_;
  Chunks sent_data_;
  int sent_trailers_{};
};

TEST_F(FilterManagerPerFilterBufferTest,
       RequestBodyBufferedAtDifferentFiltersKeepsOrderAndFilters) {
  initializeChain(true);
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndBuffer);

  // Each filter holds only the data stopped at it.
  EXPECT_EQ("bbb", filters_[0]->decoder_callbacks_->decodingBuffer()->toString());
  EXPECT_EQ("aaa", filters_[1]->decoder_callbacks_->decodingBuffer()->toString());

  // Filter 1 resumes first: the end of stream is still held by filter 0.
  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ(std::vector<bool>{false}, records_[2].decoded_headers);
  EXPECT_EQ((Chunks{{"aaa", false}}), records_[2].decoded_data);

  // Filter 0 resumes: "bbb" still goes through filter 1 before reaching filter 2.
  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaa", false}, {"bbb", true}}), records_[1].decoded_data);
  EXPECT_EQ((Chunks{{"aaa", false}, {"bbb", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, RequestBodyBufferedAtDifferentFiltersResumedInOrder) {
  initializeChain(true);
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndBuffer);
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };

  // Filter 0 resumes first: "bbb" joins the data buffered at filter 1.
  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_TRUE(records_[2].decoded_headers.empty());
  EXPECT_EQ("aaabbb", filters_[1]->decoder_callbacks_->decodingBuffer()->toString());

  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ(std::vector<bool>{false}, records_[2].decoded_headers);
  EXPECT_EQ((Chunks{{"aaabbb", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, RequestBodySharesStreamBufferWhenPerFilterBufferDisabled) {
  initializeChain(false);
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndBuffer);

  // Both filters see the single stream-level buffer.
  EXPECT_EQ("aaabbb", filters_[0]->decoder_callbacks_->decodingBuffer()->toString());
  EXPECT_EQ("aaabbb", filters_[1]->decoder_callbacks_->decodingBuffer()->toString());

  // Resuming filter 1 then filter 0 lets "bbb" bypass filter 1 and trips a debug assert.
  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest,
       ResponseBodyBufferedAtDifferentFiltersKeepsOrderAndFilters) {
  initializeChain(true);
  bufferResponseBodyAtTwoFilters();

  EXPECT_EQ("bbb", filters_[2]->encoder_callbacks_->encodingBuffer()->toString());
  EXPECT_EQ("aaa", filters_[1]->encoder_callbacks_->encodingBuffer()->toString());

  filters_[1]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ(std::vector<bool>{false}, sent_headers_);
  EXPECT_EQ((Chunks{{"aaa", false}}), sent_data_);

  filters_[2]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ((Chunks{{"aaa", false}, {"bbb", true}}), records_[1].encoded_data);
  EXPECT_EQ((Chunks{{"aaa", false}, {"bbb", true}}), sent_data_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest,
       ResponseBodySharesStreamBufferWhenPerFilterBufferDisabled) {
  initializeChain(false);
  bufferResponseBodyAtTwoFilters();

  EXPECT_EQ("aaabbb", filters_[2]->encoder_callbacks_->encodingBuffer()->toString());
  EXPECT_EQ("aaabbb", filters_[1]->encoder_callbacks_->encodingBuffer()->toString());

  filters_[1]->encoder_callbacks_->continueEncoding();
  filters_[2]->encoder_callbacks_->continueEncoding();
  // "bbb" bypasses filter 1.
  EXPECT_EQ("aaa", concat(records_[1].encoded_data));
  EXPECT_EQ("aaabbb", concat(sent_data_));

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, BodyAddedByTwoFiltersToHeaderOnlyRequestIsCoalesced) {
  initializeChain(true);
  records_[0].decode_headers = [this](bool) {
    Buffer::OwnedImpl data("aa");
    filters_[0]->decoder_callbacks_->addDecodedData(data, false);
    return FilterHeadersStatus::Continue;
  };
  records_[1].decode_headers = [this](bool) {
    Buffer::OwnedImpl data("bb");
    filters_[1]->decoder_callbacks_->addDecodedData(data, false);
    return FilterHeadersStatus::Continue;
  };

  filter_manager_->decodeHeaders(*request_headers_, true);

  EXPECT_EQ((std::vector<bool>{true}), records_[0].decoded_headers);
  EXPECT_EQ((std::vector<bool>{false}), records_[1].decoded_headers);
  EXPECT_EQ((std::vector<bool>{false}), records_[2].decoded_headers);
  EXPECT_TRUE(records_[0].decoded_data.empty());
  EXPECT_EQ((Chunks{{"aabb", true}}), records_[1].decoded_data);
  EXPECT_EQ((Chunks{{"aabb", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, BodyAddedByTwoFiltersToHeaderOnlyResponseIsCoalesced) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[2].encode_headers = [this](bool) {
    Buffer::OwnedImpl data("aa");
    filters_[2]->encoder_callbacks_->addEncodedData(data, false);
    return FilterHeadersStatus::Continue;
  };
  records_[1].encode_headers = [this](bool) {
    Buffer::OwnedImpl data("bb");
    filters_[1]->encoder_callbacks_->addEncodedData(data, false);
    return FilterHeadersStatus::Continue;
  };

  encodeHeaders(true);

  EXPECT_EQ((std::vector<bool>{false}), records_[0].encoded_headers);
  EXPECT_EQ((Chunks{{"aabb", true}}), records_[1].encoded_data);
  EXPECT_EQ((Chunks{{"aabb", true}}), records_[0].encoded_data);
  EXPECT_EQ((std::vector<bool>{false}), sent_headers_);
  EXPECT_EQ((Chunks{{"aabb", true}}), sent_data_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, TakeBufferedRequestDataRestoresStreamOrder) {
  initializeChain(true);
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndBuffer);

  Buffer::InstancePtr data = filter_manager_->takeBufferedRequestData();
  ASSERT_NE(nullptr, data);
  EXPECT_EQ("aaabbb", data->toString());
  EXPECT_EQ(0, filters_[0]->decoder_callbacks_->decodingBuffer()->length());
  EXPECT_EQ(0, filters_[1]->decoder_callbacks_->decodingBuffer()->length());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, TakeBufferedRequestDataFromStreamBuffer) {
  initializeChain(false);
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndBuffer);

  Buffer::InstancePtr data = filter_manager_->takeBufferedRequestData();
  ASSERT_NE(nullptr, data);
  EXPECT_EQ("aaabbb", data->toString());
  EXPECT_EQ(0, filters_[1]->decoder_callbacks_->decodingBuffer()->length());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, TakeBufferedRequestDataWithoutBody) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);

  EXPECT_EQ(nullptr, filter_manager_->takeBufferedRequestData());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, SetBufferLimitAppliesToTotalOfFilterBuffers) {
  initializeChain(true);
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndWatermark);

  // The two 3-byte buffers cross the new limit together, so the stream is paused once.
  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  filters_[0]->decoder_callbacks_->setBufferLimit(2);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, SetBufferLimitAppliesToStreamBuffer) {
  initializeChain(false);
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndWatermark);

  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  filters_[0]->decoder_callbacks_->setBufferLimit(2);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, NewFilterBufferUsesCurrentBufferLimit) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(2);

  // Filter 1's buffer is created after the limit change.
  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndWatermark;
  };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", false);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, ModifyDecodingBufferModifiesOnlyOwnBuffer) {
  initializeChain(true);
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndBuffer);

  filters_[1]->decoder_callbacks_->modifyDecodingBuffer(
      [](Buffer::Instance& buffer) { buffer.add("!"); });
  EXPECT_EQ("aaa!", filters_[1]->decoder_callbacks_->decodingBuffer()->toString());
  EXPECT_EQ("bbb", filters_[0]->decoder_callbacks_->decodingBuffer()->toString());

  filters_[1]->decoder_callbacks_->continueDecoding();
  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaa!", false}, {"bbb", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, ModifyEncodingBufferModifiesOnlyOwnBuffer) {
  initializeChain(true);
  bufferResponseBodyAtTwoFilters();

  filters_[1]->encoder_callbacks_->modifyEncodingBuffer(
      [](Buffer::Instance& buffer) { buffer.add("!"); });
  EXPECT_EQ("aaa!", filters_[1]->encoder_callbacks_->encodingBuffer()->toString());
  EXPECT_EQ("bbb", filters_[2]->encoder_callbacks_->encodingBuffer()->toString());

  filters_[1]->encoder_callbacks_->continueEncoding();
  filters_[2]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ((Chunks{{"aaa!", false}, {"bbb", true}}), sent_data_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, StopAllBuffersRequestAtStoppedFilter) {
  initializeChain(true);
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopAllIterationAndBuffer; };

  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", false);
  decodeData("bbb", true);
  EXPECT_TRUE(records_[1].decoded_data.empty());
  // The data parked at the stopped filter is held in its own buffer.
  EXPECT_EQ("aaabbb", filters_[1]->decoder_callbacks_->decodingBuffer()->toString());
  EXPECT_EQ(nullptr, filters_[0]->decoder_callbacks_->decodingBuffer());

  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaabbb", true}}), records_[1].decoded_data);
  EXPECT_EQ((Chunks{{"aaabbb", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, RequestTotalAboveLimitRaisesWatermarkWhenStreaming) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(5);

  // Neither 3-byte buffer is above the limit, but their total is.
  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  EXPECT_CALL(filter_manager_callbacks_, onRequestDataTooLarge()).Times(0);
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndWatermark);

  // 3 bytes are still above half the limit.
  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark()).Times(0);
  filters_[1]->decoder_callbacks_->continueDecoding();
  testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaa", false}, {"bbb", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, RequestTotalAboveLimitSendsPayloadTooLargeWhenBuffering) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(5);

  EXPECT_CALL(filter_manager_callbacks_, onRequestDataTooLarge());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "413"), _));
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndBuffer);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, MovingBytesBetweenFilterBuffersKeepsWatermarkState) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(4);
  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndWatermark);
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndWatermark;
  };

  // "bbb" moves from filter 0's buffer into filter 1's without toggling the watermark.
  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark()).Times(0);
  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ(0, filters_[0]->decoder_callbacks_->decodingBuffer()->length());
  EXPECT_EQ("aaabbb", filters_[1]->decoder_callbacks_->decodingBuffer()->toString());
  testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  // The last filter drains the bytes.
  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaabbb", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, RaisingLimitAboveRequestTotalReleasesWatermark) {
  initializeChain(true);
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndWatermark);

  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  filters_[0]->decoder_callbacks_->setBufferLimit(2);

  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  filters_[0]->decoder_callbacks_->setBufferLimit(100);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, ResponseTotalAboveLimitCallsDownstreamWatermarkCallbacks) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  MockDownstreamWatermarkCallbacks watermark_callbacks;
  filters_[2]->decoder_callbacks_->addDownstreamWatermarkCallbacks(watermark_callbacks);
  filters_[0]->decoder_callbacks_->setBufferLimit(5);
  records_[2].encode_data = [](Buffer::Instance&, bool end_stream) {
    return end_stream ? FilterDataStatus::StopIterationAndWatermark : FilterDataStatus::Continue;
  };
  records_[1].encode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[1].encode_data = [](Buffer::Instance&, bool end_stream) {
    return end_stream ? FilterDataStatus::Continue : FilterDataStatus::StopIterationAndWatermark;
  };

  // Neither 3-byte buffer is above the limit, but their total is.
  EXPECT_CALL(watermark_callbacks, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(filter_manager_callbacks_, onResponseDataTooLarge()).Times(0);
  encodeHeaders(false);
  encodeData("aaa", false);
  encodeData("bbb", true);

  // 3 bytes are still above half the limit.
  EXPECT_CALL(watermark_callbacks, onBelowWriteBufferLowWatermark()).Times(0);
  filters_[1]->encoder_callbacks_->continueEncoding();
  testing::Mock::VerifyAndClearExpectations(&watermark_callbacks);

  EXPECT_CALL(watermark_callbacks, onBelowWriteBufferLowWatermark());
  filters_[2]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ((Chunks{{"aaa", false}, {"bbb", true}}), sent_data_);

  filters_[2]->decoder_callbacks_->removeDownstreamWatermarkCallbacks(watermark_callbacks);
  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, ResponseTotalAboveLimitReportsTooLargeWhenBuffering) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(5);

  EXPECT_CALL(filter_manager_callbacks_, onResponseDataTooLarge());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "500"), _));
  bufferResponseBodyAtTwoFilters();

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, RequestTooLargeOwnedByFilterThatExceededLimit) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(5);

  // Filter 1 buffers first; filter 0 then takes the total over the limit.
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "413"), _));
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndBuffer);
  validateFilterStateData("filter0");

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, RequestTooLargeOwnedByBufferCreatorWithSharedBuffer) {
  initializeChain(false);
  filters_[0]->decoder_callbacks_->setBufferLimit(5);

  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "413"), _));
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndBuffer);
  validateFilterStateData("filter1");

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, RequestTooLargeFromAddedDataOwnedByAddingFilter) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(2);
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  filter_manager_->decodeHeaders(*request_headers_, false);

  // Data added outside of any filter callback, e.g. from an async completion.
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "413"), _));
  Buffer::OwnedImpl data("aaa");
  filters_[1]->decoder_callbacks_->addDecodedData(data, false);
  validateFilterStateData("filter1");

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, RequestTooLargeFromLoweredLimitHasNoOwner) {
  initializeChain(true);
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndBuffer);

  EXPECT_CALL(filter_manager_callbacks_, onRequestDataTooLarge());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "413"), _));
  filter_manager_->setBufferLimit(2);
  EXPECT_FALSE(filter_manager_->streamInfo().filterState()->hasData<LocalReplyOwnerObject>(
      LocalReplyFilterStateKey));

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, ResponseTooLargeOwnedByFilterThatExceededLimit) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(5);

  // Filter 1 buffers first; filter 2 then takes the total over the limit.
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "500"), _));
  bufferResponseBodyAtTwoFilters();
  validateFilterStateData("filter2");

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, PrependAndMoveIntoFilterBufferUpdateTotal) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(5);
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndWatermark;
  };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", false);

  // The total only crosses the limit with the last of the three added bytes.
  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  filters_[1]->decoder_callbacks_->modifyDecodingBuffer([](Buffer::Instance& buffer) {
    Buffer::OwnedImpl prefix("p");
    buffer.prepend(prefix);
    Buffer::OwnedImpl suffix("ss");
    buffer.move(suffix, 1);
    buffer.move(suffix, 1, false);
  });
  EXPECT_EQ("paaass", filters_[1]->decoder_callbacks_->decodingBuffer()->toString());
  testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"paaass", false}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, ModifyAndResumeRequestBufferWithStreamLevelState) {
  initializeChain(false);
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);

  filters_[1]->decoder_callbacks_->modifyDecodingBuffer(
      [](Buffer::Instance& buffer) { buffer.add("!"); });
  // End of stream comes from the stream-level flag.
  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaa!", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, ModifyAndResumeResponseBufferWithStreamLevelState) {
  initializeChain(false);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[1].encode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  encodeHeaders(false);
  encodeData("aaa", true);

  filters_[1]->encoder_callbacks_->modifyEncodingBuffer(
      [](Buffer::Instance& buffer) { buffer.add("!"); });
  // End of stream comes from the stream-level flag.
  filters_[1]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ((Chunks{{"aaa!", true}}), sent_data_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, ResponseTooLargeFromLoweredLimitHasNoOwner) {
  initializeChain(true);
  bufferResponseBodyAtTwoFilters();

  EXPECT_CALL(filter_manager_callbacks_, onResponseDataTooLarge());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "500"), _));
  filter_manager_->setBufferLimit(2);
  EXPECT_FALSE(filter_manager_->streamInfo().filterState()->hasData<LocalReplyOwnerObject>(
      LocalReplyFilterStateKey));

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, RaisingLimitAboveStreamBufferReleasesWatermark) {
  initializeChain(false);
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndWatermark);

  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  filters_[0]->decoder_callbacks_->setBufferLimit(2);

  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  filters_[0]->decoder_callbacks_->setBufferLimit(100);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, ResponseStreamBufferAboveLimitCallsWatermarkCallbacks) {
  initializeChain(false);
  filter_manager_->decodeHeaders(*request_headers_, true);
  MockDownstreamWatermarkCallbacks watermark_callbacks;
  filters_[2]->decoder_callbacks_->addDownstreamWatermarkCallbacks(watermark_callbacks);
  filters_[0]->decoder_callbacks_->setBufferLimit(5);
  records_[1].encode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[1].encode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndWatermark;
  };

  EXPECT_CALL(watermark_callbacks, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(filter_manager_callbacks_, onResponseDataTooLarge()).Times(0);
  encodeHeaders(false);
  encodeData("aaaaaa", false);

  // Raising the limit re-applies it to the existing stream buffer.
  EXPECT_CALL(watermark_callbacks, onBelowWriteBufferLowWatermark());
  filters_[0]->decoder_callbacks_->setBufferLimit(100);

  filters_[2]->decoder_callbacks_->removeDownstreamWatermarkCallbacks(watermark_callbacks);
  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest,
       ResponseStreamBufferAboveLimitReportsTooLargeWhenBuffering) {
  initializeChain(false);
  filters_[0]->decoder_callbacks_->setBufferLimit(5);

  EXPECT_CALL(filter_manager_callbacks_, onResponseDataTooLarge());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "500"), _));
  bufferResponseBodyAtTwoFilters();

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, RequestBytesLeftByLaterFilterAreNotReplayed) {
  initializeChain(true);
  records_[0].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  // Filter 1 neither drains nor buffers what it receives.
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationNoBuffer;
  };
  filter_manager_->decodeHeaders(*request_headers_, false);

  decodeData("raw", false);
  filters_[0]->decoder_callbacks_->continueDecoding();
  // Dropped from filter 0's buffer, and filter 1 holds nothing.
  EXPECT_EQ(0, filters_[0]->decoder_callbacks_->decodingBuffer()->length());
  EXPECT_EQ(nullptr, filters_[1]->decoder_callbacks_->decodingBuffer());

  decodeData("more", false);
  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"raw", false}, {"more", false}}), records_[1].decoded_data);
  EXPECT_TRUE(records_[2].decoded_data.empty());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, ResponseBytesLeftByLaterFilterAreNotReplayed) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[2].encode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  // Filter 1 neither drains nor buffers what it receives.
  records_[1].encode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationNoBuffer;
  };
  encodeHeaders(false);

  encodeData("raw", false);
  filters_[2]->encoder_callbacks_->continueEncoding();
  // Dropped from filter 2's buffer, and filter 1 holds nothing.
  EXPECT_EQ(0, filters_[2]->encoder_callbacks_->encodingBuffer()->length());
  EXPECT_EQ(nullptr, filters_[1]->encoder_callbacks_->encodingBuffer());

  encodeData("more", false);
  filters_[2]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ((Chunks{{"raw", false}, {"more", false}}), records_[1].encoded_data);
  EXPECT_TRUE(sent_data_.empty());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, DroppingBytesLeftByLaterFilterReleasesWatermark) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(4);
  int calls = 0;
  // The first frame passes filter 0, so it buffers the second one into its own buffer.
  records_[0].decode_data = [&calls](Buffer::Instance&, bool) {
    return calls++ == 0 ? FilterDataStatus::Continue : FilterDataStatus::StopIterationAndWatermark;
  };
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationNoBuffer;
  };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("x", false);

  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  decodeData("aaaaa", false);
  testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"x", false}, {"aaaaa", false}}), records_[1].decoded_data);
  EXPECT_EQ(0, filters_[0]->decoder_callbacks_->decodingBuffer()->length());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, FilterStoppingAgainOnOwnBufferKeepsIt) {
  initializeChain(true);
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopAllIterationAndBuffer; };
  int calls = 0;
  records_[1].decode_data = [&calls](Buffer::Instance&, bool) {
    return calls++ == 0 ? FilterDataStatus::StopIterationAndBuffer : FilterDataStatus::Continue;
  };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", false);

  // Filter 1 is replayed its own buffer and stops on it again.
  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ("aaa", filters_[1]->decoder_callbacks_->decodingBuffer()->toString());
  EXPECT_TRUE(records_[2].decoded_data.empty());

  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaa", false}}), records_[2].decoded_data);
  EXPECT_EQ(0, filters_[1]->decoder_callbacks_->decodingBuffer()->length());
  EXPECT_EQ(nullptr, filters_[2]->decoder_callbacks_->decodingBuffer());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, RequestDrainedAfterPayloadTooLargeReleasesNothing) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(5);
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "413"), _));
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndBuffer);

  // No backpressure was applied for the 413, so there is none to release.
  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark()).Times(0);
  filter_manager_->setBufferLimit(100);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest,
       RequestDrainedAfterPayloadTooLargeReleasesNothingWithStreamBuffer) {
  initializeChain(false);
  filters_[0]->decoder_callbacks_->setBufferLimit(5);
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "413"), _));
  bufferRequestBodyAtTwoFilters(FilterDataStatus::StopIterationAndBuffer);

  // No backpressure was applied for the 413, so there is none to release.
  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark()).Times(0);
  filter_manager_->setBufferLimit(100);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, ResponseDrainedAfterTooLargeReleasesNothing) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(5);
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "500"), _));
  bufferResponseBodyAtTwoFilters();
  StrictMock<MockDownstreamWatermarkCallbacks> watermark_callbacks;
  filters_[2]->decoder_callbacks_->addDownstreamWatermarkCallbacks(watermark_callbacks);

  // Would hit the low-without-high ENVOY_BUG if the 500 had released backpressure.
  filter_manager_->setBufferLimit(100);
  EXPECT_FALSE(filter_manager_->aboveHighWatermark());

  filters_[2]->decoder_callbacks_->removeDownstreamWatermarkCallbacks(watermark_callbacks);
  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest,
       ResponseDrainedAfterTooLargeReleasesNothingWithStreamBuffer) {
  initializeChain(false);
  filters_[0]->decoder_callbacks_->setBufferLimit(5);
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "500"), _));
  bufferResponseBodyAtTwoFilters();
  StrictMock<MockDownstreamWatermarkCallbacks> watermark_callbacks;
  filters_[2]->decoder_callbacks_->addDownstreamWatermarkCallbacks(watermark_callbacks);

  // Would hit the low-without-high ENVOY_BUG if the 500 had released backpressure.
  filter_manager_->setBufferLimit(100);
  EXPECT_FALSE(filter_manager_->aboveHighWatermark());

  filters_[2]->decoder_callbacks_->removeDownstreamWatermarkCallbacks(watermark_callbacks);
  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, RequestBackpressureReleasedAfterStreamingFlagFlips) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(4);
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndWatermark;
  };
  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaaaa", false);

  // The stream-level flag now says buffering, but the backpressure applied above still must go.
  Buffer::OwnedImpl empty;
  filters_[1]->decoder_callbacks_->addDecodedData(empty, false);
  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  filters_[1]->decoder_callbacks_->continueDecoding();

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, InjectedDecodedHeadersWithEndStreamSetMarker) {
  initializeChain(true);
  records_[0].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  filter_manager_->decodeHeaders(*request_headers_, false);
  auto* filter = dynamic_cast<ActiveStreamDecoderFilter*>(filters_[0]->decoder_callbacks_);
  ASSERT_NE(nullptr, filter);
  EXPECT_FALSE(filter->observedEndStream());

  filters_[0]->decoder_callbacks_->injectDecodedHeadersToFilterChain(true);
  EXPECT_EQ(std::vector<bool>{true}, records_[1].decoded_headers);
  EXPECT_TRUE(filter->observedEndStream());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, InjectedEncodedHeadersWithEndStreamSetMarker) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[2].encode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  encodeHeaders(false);
  auto* filter = dynamic_cast<ActiveStreamEncoderFilter*>(filters_[2]->encoder_callbacks_);
  ASSERT_NE(nullptr, filter);
  EXPECT_FALSE(filter->observedEndStream());

  filters_[2]->encoder_callbacks_->injectEncodedHeadersToFilterChain(true);
  EXPECT_EQ(std::vector<bool>{true}, sent_headers_);
  EXPECT_TRUE(filter->observedEndStream());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, TerminalFilterKeepsBodyAddedInHeadersAfterContinue) {
  initializeChain(true);
  records_[2].decode_headers = [this](bool) {
    Buffer::OwnedImpl data("aaa");
    filters_[2]->decoder_callbacks_->addDecodedData(data, true);
    return FilterHeadersStatus::StopIteration;
  };
  filter_manager_->decodeHeaders(*request_headers_, true);

  // Nothing after the terminal filter received the body, so it is still held.
  EXPECT_EQ("aaa", filters_[2]->decoder_callbacks_->decodingBuffer()->toString());
  Buffer::InstancePtr data = filter_manager_->takeBufferedRequestData();
  ASSERT_NE(nullptr, data);
  EXPECT_EQ("aaa", data->toString());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, StopAllFilterReplayedOwnBufferKeepsItOnNoBuffer) {
  initializeChain(true);
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopAllIterationAndBuffer; };
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationNoBuffer;
  };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", false);

  // As the router does for retries, the filter keeps its own buffer.
  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaa", false}}), records_[1].decoded_data);
  EXPECT_EQ("aaa", filters_[1]->decoder_callbacks_->decodingBuffer()->toString());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, RequestBufferDrainedAfterPayloadTooLargeReleasesNothing) {
  initialize();
  std::shared_ptr<MockStreamDecoderFilter> filter(new NiceMock<MockStreamDecoderFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        createDecoderFilterFactoryCb(filter)(callbacks);
        return true;
      });
  RequestHeaderMapPtr headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders()).WillByDefault(Return(makeOptRef(*headers)));
  filter_manager_->createDownstreamFilterChain();
  filter_manager_->requestHeadersInitialized();
  filter_manager_->setBufferLimit(5);

  ON_CALL(*filter, decodeData(_, _))
      .WillByDefault(Return(FilterDataStatus::StopIterationAndBuffer));
  filter_manager_->decodeHeaders(*headers, false);
  // The buffering filter goes over the limit: a 413 instead of backpressure.
  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark()).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, onRequestDataTooLarge());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "413"), _));
  Buffer::OwnedImpl data("aaaaaa");
  filter_manager_->decodeData(data, false);

  // The buffer drops below the low watermark, but there is no backpressure to release.
  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark()).Times(0);
  filter_manager_->setBufferLimit(100);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, ResponseBufferDrainedAfterTooLargeReleasesNothing) {
  initialize();
  std::shared_ptr<MockStreamFilter> filter(new NiceMock<MockStreamFilter>());
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        createStreamFilterFactoryCb(filter)(callbacks);
        return true;
      });
  RequestHeaderMapPtr headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders()).WillByDefault(Return(makeOptRef(*headers)));
  filter_manager_->createDownstreamFilterChain();
  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*headers, true);
  filter_manager_->setBufferLimit(5);
  StrictMock<MockDownstreamWatermarkCallbacks> watermark_callbacks;
  filter->decoder_callbacks_->addDownstreamWatermarkCallbacks(watermark_callbacks);

  ON_CALL(*filter, encodeHeaders(_, _)).WillByDefault(Return(FilterHeadersStatus::StopIteration));
  ON_CALL(*filter, encodeData(_, _))
      .WillByDefault(Return(FilterDataStatus::StopIterationAndBuffer));
  filter->decoder_callbacks_->encodeHeaders(
      ResponseHeaderMapPtr{new TestResponseHeaderMapImpl{{":status", "200"}}}, false, "details");
  // The buffering filter goes over the limit: a 500 instead of backpressure.
  EXPECT_CALL(filter_manager_callbacks_, onResponseDataTooLarge());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "500"), _));
  Buffer::OwnedImpl data("aaaaaa");
  filter->decoder_callbacks_->encodeData(data, false);

  // The buffer drops below the low watermark, but there is no backpressure to release: the
  // strict watermark callbacks would fail on a low watermark without a high one.
  filter_manager_->setBufferLimit(100);
  EXPECT_FALSE(filter_manager_->aboveHighWatermark());

  filter->decoder_callbacks_->removeDownstreamWatermarkCallbacks(watermark_callbacks);
  filter_manager_->destroyFilters();
}

class FilterManagerDecodeLifecycleTest : public FilterManagerTest {
public:
  enum class DecodePhase { Headers, Data, Trailers };

  using StrictDecoderFilterPtr = std::shared_ptr<StrictMock<MockStreamDecoderFilter>>;

  StrictDecoderFilterPtr setupSingleDecoderFilter() {
    initialize();
    auto filter = std::make_shared<StrictMock<MockStreamDecoderFilter>>();
    EXPECT_CALL(*filter, setDecoderFilterCallbacks(_));
    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillOnce([&](FilterChainFactoryCallbacks& callbacks) -> bool {
          callbacks.setFilterConfigName("configName1");
          createDecoderFilterFactoryCb(filter)(callbacks);
          return true;
        });
    initHeaders();
    return filter;
  }

  std::pair<StrictDecoderFilterPtr, StrictDecoderFilterPtr> setupTwoDecoderFilters() {
    initialize();
    auto filter_1 = std::make_shared<StrictMock<MockStreamDecoderFilter>>();
    auto filter_2 = std::make_shared<StrictMock<MockStreamDecoderFilter>>();
    EXPECT_CALL(*filter_1, setDecoderFilterCallbacks(_));
    EXPECT_CALL(*filter_2, setDecoderFilterCallbacks(_));
    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillOnce([&](FilterChainFactoryCallbacks& callbacks) -> bool {
          callbacks.setFilterConfigName("configName1");
          createDecoderFilterFactoryCb(filter_1)(callbacks);
          callbacks.setFilterConfigName("configName2");
          createDecoderFilterFactoryCb(filter_2)(callbacks);
          return true;
        });
    initHeaders();
    return {filter_1, filter_2};
  }

  void initHeaders() {
    filter_manager_->createDownstreamFilterChain();
    ON_CALL(filter_manager_callbacks_, requestHeaders())
        .WillByDefault(Return(makeOptRef(*headers_)));
    ON_CALL(filter_manager_callbacks_, requestTrailers())
        .WillByDefault(
            [this]() -> RequestTrailerMapOptRef { return makeOptRefFromPtr(trailers_.get()); });
    filter_manager_->requestHeadersInitialized();
  }

  void sendData(absl::string_view data, bool end_stream) {
    Buffer::OwnedImpl body(data);
    filter_manager_->decodeData(body, end_stream);
  }

  void sendTrailers() {
    trailers_ = std::make_unique<TestRequestTrailerMapImpl>(
        TestRequestTrailerMapImpl{{"trailer", "value"}});
    filter_manager_->decodeTrailers(*trailers_);
  }

  void applyStopIterationActionInPhase(StrictMock<MockStreamDecoderFilter>& filter,
                                       DecodePhase phase, bool end_stream,
                                       std::function<void()> abort_action) {
    switch (phase) {
    case DecodePhase::Headers:
      EXPECT_CALL(filter, decodeHeaders(_, end_stream))
          .WillOnce([abort_action](RequestHeaderMap&, bool) {
            abort_action();
            return FilterHeadersStatus::StopIteration;
          });
      return;
    case DecodePhase::Data:
      EXPECT_CALL(filter, decodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
      EXPECT_CALL(filter, decodeData(_, end_stream))
          .WillOnce([abort_action](Buffer::Instance&, bool) {
            abort_action();
            return FilterDataStatus::StopIterationNoBuffer;
          });
      return;
    case DecodePhase::Trailers:
      ASSERT(end_stream);
      EXPECT_CALL(filter, decodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
      EXPECT_CALL(filter, decodeTrailers(_)).WillOnce([abort_action](RequestTrailerMap&) {
        abort_action();
        return FilterTrailersStatus::StopIteration;
      });
      return;
    }
  }

  // Runs phases up to and including `phase`
  void driveThroughPhase(DecodePhase phase, bool end_stream) {
    switch (phase) {
    case DecodePhase::Headers:
      filter_manager_->decodeHeaders(*headers_, end_stream);
      return;
    case DecodePhase::Data:
      filter_manager_->decodeHeaders(*headers_, false);
      sendData("body", end_stream);
      return;
    case DecodePhase::Trailers:
      filter_manager_->decodeHeaders(*headers_, false);
      sendTrailers();
      return;
    }
  }

  void runLocalReplyOnEndStreamTest(DecodePhase end_stream_phase) {
    auto filter = setupSingleDecoderFilter();
    EXPECT_CALL(*filter, decodeMetadata(_)).Times(0);

    InSequence s;
    applyStopIterationActionInPhase(*filter, end_stream_phase, /*end_stream=*/true, [&]() {
      if (end_stream_phase == DecodePhase::Headers) {
        MetadataMap metadata_map = {{"a", "b"}};
        filter->callbacks_->addDecodedMetadata().emplace_back(
            std::make_unique<MetadataMap>(metadata_map));
      }
      filter->callbacks_->sendLocalReply(Code::InternalServerError, "err", nullptr, std::nullopt,
                                         "err");
    });
    EXPECT_CALL(*filter, onLocalReply(_)).WillOnce(Return(LocalErrorStatus::Continue));
    EXPECT_CALL(*filter, decodeComplete());
    EXPECT_CALL(filter_manager_callbacks_, disarmRequestTimeout());
    EXPECT_CALL(*filter, onDestroy());

    driveThroughPhase(end_stream_phase, /*end_stream=*/true);
    EXPECT_EQ(nullptr, filter->callbacks_->decodingBuffer());
    filter_manager_->destroyFilters();
  }

  void runGracefulGoAwayAbortTest(DecodePhase phase, bool end_stream) {
    auto filter = setupSingleDecoderFilter();
    if (!end_stream) {
      EXPECT_CALL(filter_manager_callbacks_, disarmRequestTimeout()).Times(0);
    }

    InSequence s;
    applyStopIterationActionInPhase(*filter, phase, end_stream, [&]() {
      filter->callbacks_->sendGoAwayAndClose(/*graceful=*/true);
    });
    EXPECT_CALL(filter_manager_callbacks_, sendGoAwayAndClose(true));
    if (end_stream) {
      EXPECT_CALL(*filter, decodeComplete());
      EXPECT_CALL(filter_manager_callbacks_, disarmRequestTimeout());
    }
    EXPECT_CALL(*filter, onDestroy());

    driveThroughPhase(phase, end_stream);
    filter_manager_->destroyFilters();
  }

  void runSynchronousDestroyInDecodeTest(DecodePhase phase) {
    auto filter = setupSingleDecoderFilter();
    EXPECT_CALL(*filter, decodeComplete()).Times(0);
    EXPECT_CALL(filter_manager_callbacks_, disarmRequestTimeout()).Times(0);

    InSequence s;
    applyStopIterationActionInPhase(*filter, phase, /*end_stream=*/true, [&]() {
      filter->callbacks_->resetStream(StreamResetReason::LocalReset, "");
    });
    EXPECT_CALL(filter_manager_callbacks_, resetStream(StreamResetReason::LocalReset, ""))
        .WillOnce(
            [this](StreamResetReason, absl::string_view) { filter_manager_->destroyFilters(); });
    EXPECT_CALL(*filter, onDestroy());

    driveThroughPhase(phase, /*end_stream=*/true);
  }

  RequestHeaderMapPtr headers_{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
  RequestTrailerMapPtr trailers_;
};

TEST_F(FilterManagerDecodeLifecycleTest,
       LocalReplyOnEndStreamDisarmsOnceAndSkipsMetadataInHeaders) {
  runLocalReplyOnEndStreamTest(DecodePhase::Headers);
}

TEST_F(FilterManagerDecodeLifecycleTest, LocalReplyOnEndStreamDisarmsOnceInData) {
  runLocalReplyOnEndStreamTest(DecodePhase::Data);
}

TEST_F(FilterManagerDecodeLifecycleTest, LocalReplyOnEndStreamDisarmsOnceInTrailers) {
  runLocalReplyOnEndStreamTest(DecodePhase::Trailers);
}

TEST_F(FilterManagerDecodeLifecycleTest,
       TerminalDecoderFilterLocalReplyWithHalfCloseEnabledClosesStream) {
  ON_CALL(filter_manager_callbacks_, isHalfCloseEnabled()).WillByDefault(Return(true));
  auto filter = setupSingleDecoderFilter();

  InSequence s;
  EXPECT_CALL(*filter, decodeHeaders(_, true)).WillOnce([&](RequestHeaderMap&, bool) {
    filter->callbacks_->sendLocalReply(Code::ServiceUnavailable, "no healthy upstream", nullptr,
                                       std::nullopt, "no_healthy_upstream");
    return FilterHeadersStatus::StopIteration;
  });
  EXPECT_CALL(*filter, onLocalReply(_)).WillOnce(Return(LocalErrorStatus::Continue));
  EXPECT_CALL(*filter, decodeComplete());
  EXPECT_CALL(filter_manager_callbacks_, disarmRequestTimeout());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "503"), false));
  EXPECT_CALL(filter_manager_callbacks_, encodeData(_, true));
  EXPECT_CALL(filter_manager_callbacks_, endStream()).WillOnce([this]() {
    filter_manager_->destroyFilters();
  });
  EXPECT_CALL(*filter, onDestroy());

  filter_manager_->decodeHeaders(*headers_, true);
}

TEST_F(FilterManagerDecodeLifecycleTest, GracefulGoAwayInDecodeHeadersDisarmsOnEndStream) {
  runGracefulGoAwayAbortTest(DecodePhase::Headers, /*end_stream=*/true);
}

TEST_F(FilterManagerDecodeLifecycleTest,
       GracefulGoAwayInDecodeHeadersDoesNotDisarmBeforeEndStream) {
  runGracefulGoAwayAbortTest(DecodePhase::Headers, /*end_stream=*/false);
}

TEST_F(FilterManagerDecodeLifecycleTest, GracefulGoAwayInDecodeDataDisarmsOnEndStream) {
  runGracefulGoAwayAbortTest(DecodePhase::Data, /*end_stream=*/true);
}

TEST_F(FilterManagerDecodeLifecycleTest, GracefulGoAwayInDecodeDataDoesNotDisarmBeforeEndStream) {
  runGracefulGoAwayAbortTest(DecodePhase::Data, /*end_stream=*/false);
}

TEST_F(FilterManagerDecodeLifecycleTest, GracefulGoAwayInDecodeTrailersDisarmsTimeout) {
  runGracefulGoAwayAbortTest(DecodePhase::Trailers, /*end_stream=*/true);
}

TEST_F(FilterManagerDecodeLifecycleTest, SynchronousDestroyInDecodeHeadersSkipsDecodeComplete) {
  runSynchronousDestroyInDecodeTest(DecodePhase::Headers);
}

TEST_F(FilterManagerDecodeLifecycleTest, SynchronousDestroyInDecodeDataSkipsDecodeComplete) {
  runSynchronousDestroyInDecodeTest(DecodePhase::Data);
}

TEST_F(FilterManagerDecodeLifecycleTest, SynchronousDestroyInDecodeTrailersSkipsDecodeComplete) {
  runSynchronousDestroyInDecodeTest(DecodePhase::Trailers);
}

TEST_F(FilterManagerDecodeLifecycleTest,
       TerminalDecoderFilterBufferOverflowOnEndStreamSkipsPostDestroyDisarm) {
  auto filter = setupSingleDecoderFilter();
  filter_manager_->setBufferLimit(2);

  InSequence s;
  EXPECT_CALL(*filter, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*filter, decodeData(_, true))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*filter, decodeComplete());
  EXPECT_CALL(filter_manager_callbacks_, onRequestDataTooLarge());
  EXPECT_CALL(*filter, onLocalReply(_)).WillOnce(Return(LocalErrorStatus::Continue));
  EXPECT_CALL(filter_manager_callbacks_, disarmRequestTimeout());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "413"), false));
  EXPECT_CALL(filter_manager_callbacks_, encodeData(_, true));
  EXPECT_CALL(filter_manager_callbacks_, endStream()).WillOnce([this] {
    filter_manager_->destroyFilters();
  });
  EXPECT_CALL(*filter, onDestroy());

  filter_manager_->decodeHeaders(*headers_, false);
  sendData("aaa", true);
}

TEST_F(FilterManagerDecodeLifecycleTest,
       DecodeHeadersInjectedBodyDestroyInMaybeContinueDecodingSkipsPostDestroyDisarm) {
  auto [filter_1, filter_2] = setupTwoDecoderFilters();
  EXPECT_CALL(*filter_2, decodeComplete()).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, disarmRequestTimeout()).Times(0);

  InSequence s;
  EXPECT_CALL(*filter_1, decodeHeaders(_, true))
      .WillOnce([f1 = filter_1.get()](RequestHeaderMap&, bool) {
        Buffer::OwnedImpl body("body");
        f1->callbacks_->addDecodedData(body, false);
        return FilterHeadersStatus::Continue;
      });
  EXPECT_CALL(*filter_1, decodeComplete());
  EXPECT_CALL(*filter_2, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  EXPECT_CALL(*filter_2, decodeData(_, true))
      .WillOnce([f2 = filter_2.get()](Buffer::Instance&, bool) {
        f2->callbacks_->resetStream(StreamResetReason::LocalReset, "");
        return FilterDataStatus::StopIterationNoBuffer;
      });
  EXPECT_CALL(filter_manager_callbacks_, resetStream(StreamResetReason::LocalReset, ""))
      .WillOnce(
          [this](StreamResetReason, absl::string_view) { filter_manager_->destroyFilters(); });
  EXPECT_CALL(*filter_1, onDestroy());
  EXPECT_CALL(*filter_2, onDestroy());

  filter_manager_->decodeHeaders(*headers_, true);
}

TEST_F(FilterManagerDecodeLifecycleTest, LocalReplyFromDecodeCompleteInDecodeDataIsDeferred) {
  auto filter = setupSingleDecoderFilter();
  bool encoded_during_decode_complete = false;

  InSequence s;
  EXPECT_CALL(*filter, decodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter, decodeData(_, true)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*filter, decodeComplete()).WillOnce([&]() {
    filter->callbacks_->sendLocalReply(Code::BadRequest, "bad_body", nullptr, std::nullopt,
                                       "bad_body");
    EXPECT_FALSE(encoded_during_decode_complete);
  });
  EXPECT_CALL(*filter, onLocalReply(_)).WillOnce(Return(LocalErrorStatus::Continue));
  EXPECT_CALL(filter_manager_callbacks_, disarmRequestTimeout());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "400"), false))
      .WillOnce([&](ResponseHeaderMap&, bool) { encoded_during_decode_complete = true; });
  EXPECT_CALL(filter_manager_callbacks_, encodeData(_, true));
  EXPECT_CALL(filter_manager_callbacks_, endStream()).WillOnce([this]() {
    filter_manager_->destroyFilters();
  });
  EXPECT_CALL(*filter, onDestroy());

  filter_manager_->decodeHeaders(*headers_, false);
  sendData("body", true);
}

TEST_F(FilterManagerDecodeLifecycleTest, LocalReplyFromDecodeCompleteInDecodeTrailersIsDeferred) {
  auto [filter_1, filter_2] = setupTwoDecoderFilters();
  bool encoded_during_decode_complete = false;
  EXPECT_CALL(*filter_2, decodeData(_, _)).Times(0);

  InSequence s;
  EXPECT_CALL(*filter_1, decodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter_2, decodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter_1, decodeTrailers(_)).WillOnce(Return(FilterTrailersStatus::Continue));
  EXPECT_CALL(*filter_1, decodeComplete());
  EXPECT_CALL(*filter_2, decodeTrailers(_)).WillOnce(Return(FilterTrailersStatus::Continue));
  EXPECT_CALL(*filter_2, decodeComplete())
      .WillOnce([&, f1 = filter_1.get(), f2 = filter_2.get()]() {
        Buffer::OwnedImpl extra("extra");
        f1->callbacks_->injectDecodedDataToFilterChain(extra, false);
        f2->callbacks_->sendLocalReply(Code::BadRequest, "bad_trailers", nullptr, std::nullopt,
                                       "bad_trailers");
        EXPECT_FALSE(encoded_during_decode_complete);
      });
  EXPECT_CALL(*filter_1, onLocalReply(_)).WillOnce(Return(LocalErrorStatus::Continue));
  EXPECT_CALL(*filter_2, onLocalReply(_)).WillOnce(Return(LocalErrorStatus::Continue));
  EXPECT_CALL(filter_manager_callbacks_, disarmRequestTimeout());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "400"), false))
      .WillOnce([&](ResponseHeaderMap&, bool) { encoded_during_decode_complete = true; });
  EXPECT_CALL(filter_manager_callbacks_, encodeData(_, true));
  EXPECT_CALL(filter_manager_callbacks_, endStream()).WillOnce([this]() {
    filter_manager_->destroyFilters();
  });
  EXPECT_CALL(*filter_1, onDestroy());
  EXPECT_CALL(*filter_2, onDestroy());

  filter_manager_->decodeHeaders(*headers_, false);
  sendTrailers();
}

TEST_F(FilterManagerDecodeLifecycleTest, ResetFromDecodeCompleteInDecodeDataStopsIteration) {
  auto [filter_1, filter_2] = setupTwoDecoderFilters();
  EXPECT_CALL(*filter_2, decodeData(_, _)).Times(0);

  InSequence s;
  EXPECT_CALL(*filter_1, decodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter_2, decodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter_1, decodeData(_, true)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*filter_1, decodeComplete()).WillOnce([f1 = filter_1.get()]() {
    f1->callbacks_->resetStream(StreamResetReason::LocalReset, "");
  });
  EXPECT_CALL(filter_manager_callbacks_, resetStream(StreamResetReason::LocalReset, ""))
      .WillOnce([this](StreamResetReason, absl::string_view) {
        filter_manager_->onDownstreamReset();
        filter_manager_->destroyFilters();
      });
  EXPECT_CALL(*filter_1, onDestroy());
  EXPECT_CALL(*filter_2, onDestroy());

  filter_manager_->decodeHeaders(*headers_, false);
  sendData("body", true);
}

TEST_F(FilterManagerDecodeLifecycleTest, ResetFromDecodeCompleteInDecodeTrailersStopsIteration) {
  auto [filter_1, filter_2] = setupTwoDecoderFilters();
  EXPECT_CALL(*filter_2, decodeTrailers(_)).Times(0);

  InSequence s;
  EXPECT_CALL(*filter_1, decodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter_2, decodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter_1, decodeTrailers(_)).WillOnce(Return(FilterTrailersStatus::Continue));
  EXPECT_CALL(*filter_1, decodeComplete()).WillOnce([f1 = filter_1.get()]() {
    f1->callbacks_->resetStream(StreamResetReason::LocalReset, "");
  });
  EXPECT_CALL(filter_manager_callbacks_, resetStream(StreamResetReason::LocalReset, ""))
      .WillOnce([this](StreamResetReason, absl::string_view) {
        filter_manager_->onDownstreamReset();
        filter_manager_->destroyFilters();
      });
  EXPECT_CALL(*filter_1, onDestroy());
  EXPECT_CALL(*filter_2, onDestroy());

  filter_manager_->decodeHeaders(*headers_, false);
  sendTrailers();
}

TEST_F(FilterManagerPerFilterBufferTest, InjectedDecodedDataKeepsStreamEndStream) {
  // The legacy flag would rewrite the stream-level flag; the positional marker ignores it.
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.ext_proc_inject_data_with_state_update", "true"}});
  initializeChain(true);
  records_[1].decode_data = [](Buffer::Instance& data, bool) {
    data.drain(data.length());
    return FilterDataStatus::StopIterationNoBuffer;
  };

  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);
  EXPECT_TRUE(filter_manager_->decoderObservedEndStream());

  Buffer::OwnedImpl chunk1("a1");
  filters_[1]->decoder_callbacks_->injectDecodedDataToFilterChain(chunk1, false);
  EXPECT_TRUE(filter_manager_->decoderObservedEndStream());
  Buffer::OwnedImpl chunk2("a2");
  filters_[1]->decoder_callbacks_->injectDecodedDataToFilterChain(chunk2, true);
  EXPECT_TRUE(filter_manager_->decoderObservedEndStream());
  EXPECT_EQ((Chunks{{"a1", false}, {"a2", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

class FilterManagerTrailersTest : public FilterManagerTest {
public:
  using Chunks = std::vector<std::pair<std::string, bool>>;

  struct FilterRecord {
    std::function<FilterHeadersStatus(bool)> decode_headers = [](bool) {
      return FilterHeadersStatus::Continue;
    };
    std::function<FilterDataStatus(Buffer::Instance&, bool)> decode_data =
        [](Buffer::Instance&, bool) { return FilterDataStatus::Continue; };
    std::function<FilterTrailersStatus()> decode_trailers = [] {
      return FilterTrailersStatus::Continue;
    };
    std::function<FilterHeadersStatus(bool)> encode_headers = [](bool) {
      return FilterHeadersStatus::Continue;
    };
    std::function<FilterDataStatus(Buffer::Instance&, bool)> encode_data =
        [](Buffer::Instance&, bool) { return FilterDataStatus::Continue; };
    std::function<FilterTrailersStatus()> encode_trailers = [] {
      return FilterTrailersStatus::Continue;
    };
    std::vector<bool> decoded_headers;
    Chunks decoded_data;
    int decoded_trailers{};
    std::vector<bool> encoded_headers;
    Chunks encoded_data;
    int encoded_trailers{};
  };

  void initializeChain(bool forward_held_trailers) {
    scoped_runtime_.mergeValues({{"envoy.reloadable_features.filter_manager_forward_held_trailers",
                                  forward_held_trailers ? "true" : "false"}});
    initialize();

    // The last filter consumes the request body, as the router does.
    records_[2].decode_data = [](Buffer::Instance& data, bool) {
      data.drain(data.length());
      return FilterDataStatus::Continue;
    };
    for (size_t i = 0; i < filters_.size(); i++) {
      filters_[i] = std::make_shared<NiceMock<MockStreamFilter>>();
      FilterRecord& record = records_[i];
      ON_CALL(*filters_[i], decodeHeaders(_, _))
          .WillByDefault(Invoke([&record](RequestHeaderMap&, bool end_stream) {
            record.decoded_headers.push_back(end_stream);
            return record.decode_headers(end_stream);
          }));
      ON_CALL(*filters_[i], decodeData(_, _))
          .WillByDefault(Invoke([&record](Buffer::Instance& data, bool end_stream) {
            record.decoded_data.emplace_back(data.toString(), end_stream);
            return record.decode_data(data, end_stream);
          }));
      ON_CALL(*filters_[i], decodeTrailers(_)).WillByDefault(Invoke([&record](RequestTrailerMap&) {
        record.decoded_trailers++;
        return record.decode_trailers();
      }));
      ON_CALL(*filters_[i], encodeHeaders(_, _))
          .WillByDefault(Invoke([&record](ResponseHeaderMap&, bool end_stream) {
            record.encoded_headers.push_back(end_stream);
            return record.encode_headers(end_stream);
          }));
      ON_CALL(*filters_[i], encodeData(_, _))
          .WillByDefault(Invoke([&record](Buffer::Instance& data, bool end_stream) {
            record.encoded_data.emplace_back(data.toString(), end_stream);
            return record.encode_data(data, end_stream);
          }));
      ON_CALL(*filters_[i], encodeTrailers(_)).WillByDefault(Invoke([&record](ResponseTrailerMap&) {
        record.encoded_trailers++;
        return record.encode_trailers();
      }));
    }

    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillRepeatedly(Invoke([this](FilterChainFactoryCallbacks& callbacks) -> bool {
          // The filter manager keeps a view of the name, so it must outlive the stream.
          constexpr std::array<absl::string_view, 3> names{"filter0", "filter1", "filter2"};
          for (size_t i = 0; i < filters_.size(); i++) {
            callbacks.setFilterConfigName(names[i]);
            createStreamFilterFactoryCb(filters_[i])(callbacks);
          }
          return true;
        }));

    ON_CALL(filter_manager_callbacks_, requestHeaders())
        .WillByDefault(Return(makeOptRef(*request_headers_)));
    ON_CALL(filter_manager_callbacks_, requestTrailers())
        .WillByDefault(Invoke([this]() -> RequestTrailerMapOptRef {
          return makeOptRefFromPtr(request_trailers_.get());
        }));
    ON_CALL(filter_manager_callbacks_, setRequestTrailers(_))
        .WillByDefault(Invoke(
            [this](RequestTrailerMapPtr&& trailers) { request_trailers_ = std::move(trailers); }));
    ON_CALL(filter_manager_callbacks_, encodeHeaders(_, _))
        .WillByDefault(Invoke(
            [this](ResponseHeaderMap&, bool end_stream) { sent_headers_.push_back(end_stream); }));
    ON_CALL(filter_manager_callbacks_, encodeData(_, _))
        .WillByDefault(Invoke([this](Buffer::Instance& data, bool end_stream) {
          sent_data_.emplace_back(data.toString(), end_stream);
          data.drain(data.length());
        }));
    ON_CALL(filter_manager_callbacks_, encodeTrailers(_))
        .WillByDefault(Invoke([this](ResponseTrailerMap&) { sent_trailers_++; }));

    filter_manager_->createDownstreamFilterChain();
    filter_manager_->requestHeadersInitialized();
  }

  void decodeData(absl::string_view body, bool end_stream) {
    Buffer::OwnedImpl data(body);
    filter_manager_->decodeData(data, end_stream);
  }

  void decodeTrailers() {
    request_trailers_ = std::make_unique<TestRequestTrailerMapImpl>(
        TestRequestTrailerMapImpl{{"trailer", "value"}});
    filter_manager_->decodeTrailers(*request_trailers_);
  }

  // The response is originated by the last decoder filter, i.e. the first encoder filter.
  void encodeHeaders(bool end_stream) {
    filters_[2]->decoder_callbacks_->encodeHeaders(
        std::make_unique<TestResponseHeaderMapImpl>(TestResponseHeaderMapImpl{{":status", "200"}}),
        end_stream, "details");
  }

  void encodeData(absl::string_view body, bool end_stream) {
    Buffer::OwnedImpl data(body);
    filters_[2]->decoder_callbacks_->encodeData(data, end_stream);
  }

  void encodeTrailers() {
    filters_[2]->decoder_callbacks_->encodeTrailers(std::make_unique<TestResponseTrailerMapImpl>(
        TestResponseTrailerMapImpl{{"trailer", "value"}}));
  }

  TestScopedRuntime scoped_runtime_;
  std::array<std::shared_ptr<MockStreamFilter>, 3> filters_;
  std::array<FilterRecord, 3> records_;
  RequestHeaderMapPtr request_headers_{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
  RequestTrailerMapPtr request_trailers_;
  std::vector<bool> sent_headers_;
  Chunks sent_data_;
  int sent_trailers_{};
};

TEST_F(FilterManagerTrailersTest, RequestTrailersHeldByEarlierFilterAreNotForwardedEarly) {
  initializeChain(true);
  records_[0].decode_trailers = [] { return FilterTrailersStatus::StopIteration; };
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };

  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", false);
  decodeTrailers();

  // Trailers are held by filter 0, so filter 1 forwards only its data.
  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaa", false}}), records_[2].decoded_data);
  EXPECT_EQ(0, records_[1].decoded_trailers);
  EXPECT_EQ(0, records_[2].decoded_trailers);

  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ(1, records_[1].decoded_trailers);
  EXPECT_EQ(1, records_[2].decoded_trailers);
  // Filter 0 replays the drained shared buffer as an empty frame before the trailers.
  EXPECT_EQ((Chunks{{"aaa", false}, {"", false}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, RequestTrailersReachingStoppedFilterAreForwardedOnResume) {
  initializeChain(true);
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  records_[1].decode_trailers = [] { return FilterTrailersStatus::StopIteration; };

  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", false);
  decodeTrailers();
  EXPECT_EQ(1, records_[1].decoded_trailers);

  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaa", false}}), records_[2].decoded_data);
  EXPECT_EQ(1, records_[2].decoded_trailers);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, ResponseTrailersHeldByEarlierFilterAreNotForwardedEarly) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[2].encode_trailers = [] { return FilterTrailersStatus::StopIteration; };
  records_[1].encode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[1].encode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };

  encodeHeaders(false);
  encodeData("aaa", false);
  encodeTrailers();

  filters_[1]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ((Chunks{{"aaa", false}}), sent_data_);
  EXPECT_EQ(0, records_[1].encoded_trailers);
  EXPECT_EQ(0, sent_trailers_);

  filters_[2]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ(1, records_[1].encoded_trailers);
  EXPECT_EQ(1, sent_trailers_);
  // Filter 2 replays the drained shared buffer as an empty frame before the trailers.
  EXPECT_EQ((Chunks{{"aaa", false}, {"", false}}), sent_data_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, RequestTrailersAddedBeforeStopAllFilterAreForwarded) {
  initializeChain(true);
  records_[0].decode_data = [this](Buffer::Instance&, bool end_stream) {
    if (end_stream) {
      filters_[0]->decoder_callbacks_->addDecodedTrailers().addCopy(LowerCaseString("added"), "1");
    }
    return FilterDataStatus::Continue;
  };
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopAllIterationAndBuffer; };

  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);
  EXPECT_EQ(0, records_[1].decoded_trailers);

  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaa", false}}), records_[2].decoded_data);
  EXPECT_EQ(1, records_[1].decoded_trailers);
  EXPECT_EQ(1, records_[2].decoded_trailers);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, ResponseTrailersAddedBeforeStoppedFilterAreForwarded) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[2].encode_data = [this](Buffer::Instance&, bool end_stream) {
    if (end_stream) {
      filters_[2]->encoder_callbacks_->addEncodedTrailers().addCopy(LowerCaseString("added"), "1");
    }
    return FilterDataStatus::Continue;
  };
  records_[1].encode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  records_[1].encode_trailers = [] { return FilterTrailersStatus::StopIteration; };

  encodeHeaders(false);
  encodeData("aaa", true);
  // Filter 1 gets the trailers while it holds the data.
  EXPECT_EQ(1, records_[1].encoded_trailers);
  EXPECT_EQ(0, sent_trailers_);

  filters_[1]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ((Chunks{{"aaa", false}}), sent_data_);
  EXPECT_EQ(1, records_[0].encoded_trailers);
  EXPECT_EQ(1, sent_trailers_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, ResponseTrailersAddedBeforeStopAllFilterAreForwarded) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[2].encode_data = [this](Buffer::Instance&, bool end_stream) {
    if (end_stream) {
      filters_[2]->encoder_callbacks_->addEncodedTrailers().addCopy(LowerCaseString("added"), "1");
    }
    return FilterDataStatus::Continue;
  };
  records_[1].encode_headers = [](bool) { return FilterHeadersStatus::StopAllIterationAndBuffer; };

  encodeHeaders(false);
  encodeData("aaa", true);
  EXPECT_EQ(0, records_[1].encoded_trailers);

  filters_[1]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ((Chunks{{"aaa", false}}), sent_data_);
  EXPECT_EQ(1, records_[1].encoded_trailers);
  EXPECT_EQ(1, sent_trailers_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, RequestTrailersAddedByFilterContinuingOnDataAreDecodedOnce) {
  initializeChain(true);
  records_[0].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[0].decode_data = [this](Buffer::Instance&, bool) {
    filters_[0]->decoder_callbacks_->addDecodedTrailers().addCopy(LowerCaseString("added"), "1");
    return FilterDataStatus::Continue;
  };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);

  EXPECT_EQ((Chunks{{"aaa", false}}), records_[1].decoded_data);
  EXPECT_EQ(1, records_[1].decoded_trailers);
  EXPECT_EQ(1, records_[2].decoded_trailers);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, RequestTrailersAddedByStoppingFilterFollowItsData) {
  initializeChain(true);
  records_[0].decode_data = [this](Buffer::Instance&, bool) {
    filters_[0]->decoder_callbacks_->addDecodedTrailers().addCopy(LowerCaseString("added"), "1");
    return FilterDataStatus::StopIterationAndBuffer;
  };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);
  EXPECT_TRUE(records_[1].decoded_data.empty());
  EXPECT_EQ(0, records_[1].decoded_trailers);

  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaa", false}}), records_[2].decoded_data);
  EXPECT_EQ(1, records_[1].decoded_trailers);
  EXPECT_EQ(1, records_[2].decoded_trailers);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, ResponseTrailersAddedByStoppingFilterFollowItsData) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[2].encode_data = [this](Buffer::Instance&, bool) {
    filters_[2]->encoder_callbacks_->addEncodedTrailers().addCopy(LowerCaseString("added"), "1");
    return FilterDataStatus::StopIterationAndBuffer;
  };
  encodeHeaders(false);
  encodeData("aaa", true);
  EXPECT_TRUE(sent_data_.empty());
  EXPECT_EQ(0, records_[1].encoded_trailers);

  filters_[2]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ((Chunks{{"aaa", false}}), sent_data_);
  EXPECT_EQ(1, records_[1].encoded_trailers);
  EXPECT_EQ(1, sent_trailers_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, ResponseTrailersAddedByFilterContinuingOnDataAreEncodedOnce) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[2].encode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[2].encode_data = [this](Buffer::Instance&, bool) {
    filters_[2]->encoder_callbacks_->addEncodedTrailers().addCopy(LowerCaseString("added"), "1");
    return FilterDataStatus::Continue;
  };
  encodeHeaders(false);
  encodeData("aaa", true);

  EXPECT_EQ((Chunks{{"aaa", false}}), sent_data_);
  EXPECT_EQ(1, records_[1].encoded_trailers);
  EXPECT_EQ(1, sent_trailers_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, RequestTrailersAddedBeforeStoppedFilterReachIt) {
  initializeChain(true);
  records_[0].decode_data = [this](Buffer::Instance&, bool) {
    filters_[0]->decoder_callbacks_->addDecodedTrailers().addCopy(LowerCaseString("added"), "1");
    return FilterDataStatus::Continue;
  };
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  records_[1].decode_trailers = [] { return FilterTrailersStatus::StopIteration; };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);
  // Filter 1 gets the trailers while it holds the data.
  EXPECT_EQ(1, records_[1].decoded_trailers);
  EXPECT_EQ(0, records_[2].decoded_trailers);

  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaa", false}}), records_[2].decoded_data);
  EXPECT_EQ(1, records_[2].decoded_trailers);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, RequestTrailersAddedBeforeFilterContinuingOnDataAreDecodedOnce) {
  initializeChain(true);
  records_[0].decode_data = [this](Buffer::Instance&, bool) {
    filters_[0]->decoder_callbacks_->addDecodedTrailers().addCopy(LowerCaseString("added"), "1");
    return FilterDataStatus::Continue;
  };
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);

  EXPECT_EQ(1, records_[1].decoded_trailers);
  EXPECT_EQ((Chunks{{"aaa", false}}), records_[2].decoded_data);
  EXPECT_EQ(1, records_[2].decoded_trailers);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, ResponseTrailersAddedBeforeFilterContinuingOnDataAreEncodedOnce) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[2].encode_data = [this](Buffer::Instance&, bool) {
    filters_[2]->encoder_callbacks_->addEncodedTrailers().addCopy(LowerCaseString("added"), "1");
    return FilterDataStatus::Continue;
  };
  records_[1].encode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  encodeHeaders(false);
  encodeData("aaa", true);

  // Without the guard, filter 1 never receives these trailers.
  EXPECT_EQ(1, records_[1].encoded_trailers);
  EXPECT_EQ(1, records_[0].encoded_trailers);
  EXPECT_EQ((Chunks{{"aaa", false}}), sent_data_);
  EXPECT_EQ(1, sent_trailers_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, RequestTrailersAddedBeforeStopAllFilterWaitForItToResume) {
  initializeChain(true);
  records_[0].decode_data = [this](Buffer::Instance&, bool) {
    filters_[0]->decoder_callbacks_->addDecodedTrailers().addCopy(LowerCaseString("added"), "1");
    return FilterDataStatus::Continue;
  };
  records_[2].decode_headers = [](bool) { return FilterHeadersStatus::StopAllIterationAndBuffer; };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);
  EXPECT_EQ(0, records_[1].decoded_trailers);
  EXPECT_EQ(0, records_[2].decoded_trailers);

  // Filter 1 gets the trailers too, after its data.
  filters_[2]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaa", false}}), records_[2].decoded_data);
  EXPECT_EQ(1, records_[1].decoded_trailers);
  EXPECT_EQ(1, records_[2].decoded_trailers);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, ResponseTrailersAddedBeforeStopAllFilterWaitForItToResume) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[2].encode_data = [this](Buffer::Instance&, bool) {
    filters_[2]->encoder_callbacks_->addEncodedTrailers().addCopy(LowerCaseString("added"), "1");
    return FilterDataStatus::Continue;
  };
  records_[0].encode_headers = [](bool) { return FilterHeadersStatus::StopAllIterationAndBuffer; };
  encodeHeaders(false);
  encodeData("aaa", true);
  EXPECT_EQ(0, records_[1].encoded_trailers);
  EXPECT_EQ(0, records_[0].encoded_trailers);
  EXPECT_EQ(0, sent_trailers_);

  // Filter 1 gets the trailers too, after its data.
  filters_[0]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ(1, records_[1].encoded_trailers);
  EXPECT_EQ((Chunks{{"aaa", false}}), sent_data_);
  EXPECT_EQ(1, records_[0].encoded_trailers);
  EXPECT_EQ(1, sent_trailers_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest,
       ResponseTrailersAddedBeforeStoppedFilterNeverReachItWithoutGuard) {
  initializeChain(false);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[2].encode_data = [this](Buffer::Instance&, bool) {
    filters_[2]->encoder_callbacks_->addEncodedTrailers().addCopy(LowerCaseString("added"), "1");
    return FilterDataStatus::Continue;
  };
  records_[1].encode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  encodeHeaders(false);
  encodeData("aaa", true);
  // Filter 1 never gets the trailers; they are sent from the filter after it on resume.
  EXPECT_EQ(0, records_[1].encoded_trailers);

  filters_[1]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ(0, records_[1].encoded_trailers);
  EXPECT_EQ((Chunks{{"aaa", false}}), sent_data_);
  EXPECT_EQ(1, sent_trailers_);

  filter_manager_->destroyFilters();
}

enum class TrailersDirection { Request, Response };

enum class TrailersAddPhase { None, Data, StoppingData, HeaderOnlyHeaders };

enum class TrailersStopKind {
  None,
  BufferData,
  ContinueOnData,
  StopAll,
  StopTrailers,
  // Stops all iteration on headers, buffers the data on resume, and continues on trailers.
  StopAllThenBufferData,
};

struct TrailersCase {
  TrailersDirection direction;
  TrailersAddPhase add_phase;
  // Position in data-flow order of the filter adding the trailers, ignored for None.
  int adder;
  TrailersStopKind stop_kind;
  // Position in data-flow order of the filter stopping, after the adder.
  int stopper;
};

std::string trailersCaseName(const testing::TestParamInfo<TrailersCase>& info) {
  constexpr std::array<absl::string_view, 4> phases{"CodecTrailers", "AddInData",
                                                    "AddInStoppingData", "AddInHeaderOnly"};
  constexpr std::array<absl::string_view, 6> kinds{
      "NoStop", "BufferData", "ContinueOnData", "StopAll", "StopTrailers", "StopAllThenBufferData"};
  const TrailersCase& c = info.param;
  return absl::StrCat(c.direction == TrailersDirection::Request ? "Request" : "Response",
                      phases[static_cast<int>(c.add_phase)],
                      c.add_phase == TrailersAddPhase::None ? "" : absl::StrCat("At", c.adder),
                      kinds[static_cast<int>(c.stop_kind)],
                      c.stop_kind == TrailersStopKind::None ? "" : absl::StrCat("At", c.stopper));
}

std::vector<TrailersCase> allTrailersCases() {
  std::vector<TrailersCase> cases;
  for (TrailersDirection direction : {TrailersDirection::Request, TrailersDirection::Response}) {
    for (TrailersAddPhase phase :
         {TrailersAddPhase::None, TrailersAddPhase::Data, TrailersAddPhase::StoppingData,
          TrailersAddPhase::HeaderOnlyHeaders}) {
      for (int adder = 0; adder < 3; adder++) {
        if (phase == TrailersAddPhase::None && adder > 0) {
          break;
        }
        cases.push_back({direction, phase, adder, TrailersStopKind::None, 0});
        for (TrailersStopKind kind :
             {TrailersStopKind::BufferData, TrailersStopKind::ContinueOnData,
              TrailersStopKind::StopAll, TrailersStopKind::StopTrailers,
              TrailersStopKind::StopAllThenBufferData}) {
          // Codec trailers can be stopped anywhere; added trailers only after the adder.
          for (int stopper = phase == TrailersAddPhase::None ? 0 : adder + 1; stopper < 3;
               stopper++) {
            cases.push_back({direction, phase, adder, kind, stopper});
          }
        }
      }
    }
  }
  return cases;
}

class FilterManagerTrailersInvariantTest : public FilterManagerTrailersTest,
                                           public testing::WithParamInterface<TrailersCase> {
public:
  bool request() const { return GetParam().direction == TrailersDirection::Request; }
  // Filter index at a position in data-flow order.
  size_t filterAt(int position) const { return request() ? position : 2 - position; }

  void log(size_t filter, absl::string_view event) {
    events_[filter].push_back(std::string(event));
  }

  void configureAdder() {
    const TrailersCase& c = GetParam();
    if (c.add_phase == TrailersAddPhase::None) {
      return;
    }
    const size_t i = filterAt(c.adder);
    const bool stops = c.add_phase == TrailersAddPhase::StoppingData;
    if (c.add_phase == TrailersAddPhase::HeaderOnlyHeaders) {
      auto add = [this, i](bool end_stream) {
        Buffer::OwnedImpl body("aaa");
        if (request()) {
          filters_[i]->decoder_callbacks_->addDecodedData(body, false);
          filters_[i]->decoder_callbacks_->addDecodedTrailers().addCopy(LowerCaseString("a"), "1");
        } else {
          filters_[i]->encoder_callbacks_->addEncodedData(body, false);
          filters_[i]->encoder_callbacks_->addEncodedTrailers().addCopy(LowerCaseString("a"), "1");
        }
        EXPECT_TRUE(end_stream);
        return FilterHeadersStatus::Continue;
      };
      (request() ? records_[i].decode_headers : records_[i].encode_headers) = add;
      return;
    }
    auto add = [this, i, stops](Buffer::Instance&, bool end_stream) {
      if (end_stream) {
        if (request()) {
          filters_[i]->decoder_callbacks_->addDecodedTrailers().addCopy(LowerCaseString("a"), "1");
        } else {
          filters_[i]->encoder_callbacks_->addEncodedTrailers().addCopy(LowerCaseString("a"), "1");
        }
      }
      return stops ? FilterDataStatus::StopIterationAndBuffer : FilterDataStatus::Continue;
    };
    (request() ? records_[i].decode_data : records_[i].encode_data) = add;
  }

  void configureStopper() {
    const TrailersCase& c = GetParam();
    const size_t i = filterAt(c.stopper);
    auto& headers = request() ? records_[i].decode_headers : records_[i].encode_headers;
    auto& data = request() ? records_[i].decode_data : records_[i].encode_data;
    auto& trailers = request() ? records_[i].decode_trailers : records_[i].encode_trailers;
    switch (c.stop_kind) {
    case TrailersStopKind::None:
      break;
    case TrailersStopKind::BufferData:
      data = [](Buffer::Instance&, bool) { return FilterDataStatus::StopIterationAndBuffer; };
      break;
    case TrailersStopKind::ContinueOnData:
      headers = [](bool) { return FilterHeadersStatus::StopIteration; };
      data = [](Buffer::Instance&, bool) { return FilterDataStatus::Continue; };
      break;
    case TrailersStopKind::StopAll:
      headers = [](bool) { return FilterHeadersStatus::StopAllIterationAndBuffer; };
      break;
    case TrailersStopKind::StopTrailers:
      trailers = [] { return FilterTrailersStatus::StopIteration; };
      break;
    case TrailersStopKind::StopAllThenBufferData:
      headers = [](bool) { return FilterHeadersStatus::StopAllIterationAndBuffer; };
      data = [](Buffer::Instance&, bool) { return FilterDataStatus::StopIterationAndBuffer; };
      break;
    }
  }

  // Resumes the filter unless it already resumed on its own, e.g. by continuing on trailers.
  void resume(size_t i) {
    if (request()) {
      auto* filter = dynamic_cast<ActiveStreamDecoderFilter*>(filters_[i]->decoder_callbacks_);
      if (!filter->canIterate()) {
        filters_[i]->decoder_callbacks_->continueDecoding();
      }
    } else {
      auto* filter = dynamic_cast<ActiveStreamEncoderFilter*>(filters_[i]->encoder_callbacks_);
      if (!filter->canIterate()) {
        filters_[i]->encoder_callbacks_->continueEncoding();
      }
    }
  }

  void run() {
    const TrailersCase& c = GetParam();
    initializeChain(true);
    configureAdder();
    configureStopper();
    // The terminal request filter drains the body; the codec is the response sink.
    for (size_t i = 0; i < 3; i++) {
      auto log_data = [this, i](std::function<FilterDataStatus(Buffer::Instance&, bool)>& f) {
        f = [this, i, inner = f](Buffer::Instance& data, bool end_stream) {
          log(i, absl::StrCat("data:", data.toString(), end_stream ? ":end" : ""));
          return inner(data, end_stream);
        };
      };
      auto log_trailers = [this, i](std::function<FilterTrailersStatus()>& f) {
        f = [this, i, inner = f] {
          log(i, "trailers");
          return inner();
        };
      };
      log_data(request() ? records_[i].decode_data : records_[i].encode_data);
      log_trailers(request() ? records_[i].decode_trailers : records_[i].encode_trailers);
    }
    ON_CALL(filter_manager_callbacks_, encodeData(_, _))
        .WillByDefault(Invoke([this](Buffer::Instance& data, bool end_stream) {
          log(3, absl::StrCat("data:", data.toString(), end_stream ? ":end" : ""));
          data.drain(data.length());
        }));
    ON_CALL(filter_manager_callbacks_, encodeTrailers(_))
        .WillByDefault(Invoke([this](ResponseTrailerMap&) { log(3, "trailers"); }));

    const bool header_only = c.add_phase == TrailersAddPhase::HeaderOnlyHeaders;
    if (request()) {
      filter_manager_->decodeHeaders(*request_headers_, header_only);
      if (!header_only) {
        decodeData("aaa", c.add_phase != TrailersAddPhase::None);
        if (c.add_phase == TrailersAddPhase::None) {
          decodeTrailers();
        }
      }
    } else {
      filter_manager_->decodeHeaders(*request_headers_, true);
      encodeHeaders(header_only);
      if (!header_only) {
        encodeData("aaa", c.add_phase != TrailersAddPhase::None);
        if (c.add_phase == TrailersAddPhase::None) {
          encodeTrailers();
        }
      }
    }
    // Resume whatever stopped, the adder first.
    if (c.add_phase == TrailersAddPhase::StoppingData) {
      resume(filterAt(c.adder));
    }
    if (c.stop_kind == TrailersStopKind::BufferData) {
      // A filter that buffers until the end of the stream resumes only once the trailers reach it.
      const size_t i = filterAt(c.stopper);
      EXPECT_TRUE(request()
                      ? dynamic_cast<ActiveStreamDecoderFilter*>(filters_[i]->decoder_callbacks_)
                            ->canIterate()
                      : dynamic_cast<ActiveStreamEncoderFilter*>(filters_[i]->encoder_callbacks_)
                            ->canIterate());
    } else if (c.stop_kind != TrailersStopKind::None &&
               c.stop_kind != TrailersStopKind::ContinueOnData) {
      resume(filterAt(c.stopper));
    }

    verify();
    filter_manager_->destroyFilters();
  }

  // Every filter the trailers pass through, and the response sink, sees the whole body exactly
  // once, never with end_stream, and then the trailers exactly once.
  void verifyReceiver(size_t receiver, bool saw_body) {
    SCOPED_TRACE(receiver == 3 ? std::string("codec") : absl::StrCat("filter", receiver));
    const std::vector<std::string>& events = events_[receiver];
    std::string body;
    int trailers = 0;
    for (const std::string& event : events) {
      if (event == "trailers") {
        trailers++;
        continue;
      }
      EXPECT_EQ(0, trailers) << "data after trailers: " << absl::StrJoin(events, ",");
      EXPECT_FALSE(absl::EndsWith(event, ":end")) << absl::StrJoin(events, ",");
      body += event.substr(5);
    }
    EXPECT_EQ(saw_body ? "aaa" : "", body) << absl::StrJoin(events, ",");
    EXPECT_EQ(1, trailers) << absl::StrJoin(events, ",");
  }

  void verify() {
    const TrailersCase& c = GetParam();
    const int first = c.add_phase == TrailersAddPhase::None ? 0 : c.adder + 1;
    for (int position = first; position < 3; position++) {
      verifyReceiver(filterAt(position), true);
    }
    if (!request()) {
      verifyReceiver(3, true);
    }
    // The adder never receives its own trailers, nor do the filters before it.
    for (int position = 0; position < first; position++) {
      EXPECT_EQ(0, std::count(events_[filterAt(position)].begin(),
                              events_[filterAt(position)].end(), "trailers"));
    }
  }

  std::array<std::vector<std::string>, 4> events_;
};

INSTANTIATE_TEST_SUITE_P(AllCases, FilterManagerTrailersInvariantTest,
                         testing::ValuesIn(allTrailersCases()), trailersCaseName);

TEST_P(FilterManagerTrailersInvariantTest, TrailersFollowDataExactlyOnce) { run(); }

TEST_F(FilterManagerTrailersTest, RequestTrailersAddedByResumedFilterWaitForLastStopAll) {
  initializeChain(true);
  // Filter 0 buffers the body, then adds trailers and resumes on the last frame.
  records_[0].decode_data = [this](Buffer::Instance&, bool end_stream) {
    if (!end_stream) {
      return FilterDataStatus::StopIterationAndBuffer;
    }
    filters_[0]->decoder_callbacks_->addDecodedTrailers().addCopy(LowerCaseString("added"), "1");
    return FilterDataStatus::Continue;
  };
  records_[2].decode_headers = [](bool) { return FilterHeadersStatus::StopAllIterationAndBuffer; };

  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", false);
  decodeData("bbb", true);
  EXPECT_EQ(1, records_[1].decoded_trailers);
  // The trailers stay parked at the stopped filter.
  EXPECT_TRUE(records_[2].decoded_data.empty());
  EXPECT_EQ(0, records_[2].decoded_trailers);

  filters_[2]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaabbb", false}}), records_[2].decoded_data);
  EXPECT_EQ(1, records_[2].decoded_trailers);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, ResponseTrailersAddedByResumedFilterWaitForStopAll) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  // Filter 2 is the first encoder filter: it buffers the body, then adds trailers and resumes on
  // the last frame.
  records_[2].encode_data = [this](Buffer::Instance&, bool end_stream) {
    if (!end_stream) {
      return FilterDataStatus::StopIterationAndBuffer;
    }
    filters_[2]->encoder_callbacks_->addEncodedTrailers().addCopy(LowerCaseString("added"), "1");
    return FilterDataStatus::Continue;
  };
  records_[1].encode_headers = [](bool) { return FilterHeadersStatus::StopAllIterationAndBuffer; };

  encodeHeaders(false);
  encodeData("aaa", false);
  encodeData("bbb", true);
  // Nothing passes the stopped filter, the trailers included.
  EXPECT_TRUE(sent_headers_.empty());
  EXPECT_EQ(0, records_[1].encoded_trailers);
  EXPECT_EQ(0, records_[0].encoded_trailers);
  EXPECT_EQ(0, sent_trailers_);

  filters_[1]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ((std::vector<bool>{false}), sent_headers_);
  EXPECT_EQ((Chunks{{"aaabbb", false}}), sent_data_);
  EXPECT_EQ(1, records_[1].encoded_trailers);
  EXPECT_EQ(1, records_[0].encoded_trailers);
  EXPECT_EQ(1, sent_trailers_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, ResponseTrailersHeldAtStopAllFilterReachItOnceAfterResume) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[1].encode_headers = [](bool) { return FilterHeadersStatus::StopAllIterationAndBuffer; };
  // On resume, filter 1 buffers the replayed data and stops on the trailers.
  records_[1].encode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  records_[1].encode_trailers = [] { return FilterTrailersStatus::StopIteration; };

  encodeHeaders(false);
  encodeData("aaa", false);
  encodeTrailers();
  EXPECT_EQ(0, records_[1].encoded_trailers);

  filters_[1]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ(1, records_[1].encoded_trailers);
  EXPECT_TRUE(sent_data_.empty());
  EXPECT_EQ(0, sent_trailers_);

  filters_[1]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ((Chunks{{"aaa", false}}), sent_data_);
  EXPECT_EQ(1, records_[1].encoded_trailers);
  EXPECT_EQ(1, records_[0].encoded_trailers);
  EXPECT_EQ(1, sent_trailers_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, ResponseTrailersNotForwardedAfterTooLargeReplyEndsStream) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(2);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[2].encode_data = [this](Buffer::Instance&, bool end_stream) {
    if (end_stream) {
      filters_[2]->encoder_callbacks_->addEncodedTrailers().addCopy(LowerCaseString("added"), "1");
    }
    return FilterDataStatus::Continue;
  };
  records_[1].encode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[1].encode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };

  encodeHeaders(false);
  // The 500 goes straight to the codec and ends the stream, which destroys the filters as on
  // HTTP/1.
  EXPECT_CALL(filter_manager_callbacks_, onResponseDataTooLarge());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "500"), _));
  EXPECT_CALL(filter_manager_callbacks_, endStream()).WillOnce([this] {
    filter_manager_->destroyFilters();
  });
  encodeData("aaa", true);

  EXPECT_EQ(0, records_[1].encoded_trailers);
  EXPECT_EQ(0, records_[0].encoded_trailers);
  EXPECT_EQ(0, sent_trailers_);
}

TEST_F(FilterManagerTrailersTest, RequestTrailersNotForwardedAfterTooLargeReplyEndsStream) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(2);
  records_[0].decode_data = [this](Buffer::Instance&, bool end_stream) {
    if (end_stream) {
      filters_[0]->decoder_callbacks_->addDecodedTrailers().addCopy(LowerCaseString("added"), "1");
    }
    return FilterDataStatus::Continue;
  };
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };

  filter_manager_->decodeHeaders(*request_headers_, false);
  // The 413 ends the stream, which destroys the filters as on HTTP/1.
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "413"), _));
  EXPECT_CALL(filter_manager_callbacks_, endStream()).WillOnce([this] {
    filter_manager_->destroyFilters();
  });
  decodeData("aaa", true);

  EXPECT_EQ(0, records_[1].decoded_trailers);
  EXPECT_EQ(0, records_[2].decoded_trailers);
}

TEST_F(FilterManagerTrailersTest,
       RequestTrailersAddedByFilterContinuingOnDataAreDecodedTwiceWithoutGuard) {
  initializeChain(false);
  records_[0].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[0].decode_data = [this](Buffer::Instance&, bool) {
    filters_[0]->decoder_callbacks_->addDecodedTrailers().addCopy(LowerCaseString("added"), "1");
    return FilterDataStatus::Continue;
  };
  // Filter 1 holds the trailers, so neither dispatch completes the decoder chain.
  records_[1].decode_trailers = [] { return FilterTrailersStatus::StopIteration; };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);

  EXPECT_EQ((Chunks{{"aaa", false}}), records_[1].decoded_data);
  // Sent once by the resumed filter 0 and again from filter 0 after the data loop.
  EXPECT_EQ(2, records_[1].decoded_trailers);
  EXPECT_EQ(0, records_[2].decoded_trailers);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, ResponseTrailersAddedWithoutStopAreEncodedOnceWithoutGuard) {
  initializeChain(false);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[2].encode_data = [this](Buffer::Instance&, bool) {
    filters_[2]->encoder_callbacks_->addEncodedTrailers().addCopy(LowerCaseString("added"), "1");
    return FilterDataStatus::Continue;
  };
  encodeHeaders(false);
  encodeData("aaa", true);

  // Dispatched from filter 2 after the data reaches the codec.
  EXPECT_EQ(0, records_[2].encoded_trailers);
  EXPECT_EQ(1, records_[1].encoded_trailers);
  EXPECT_EQ(1, records_[0].encoded_trailers);
  EXPECT_EQ((Chunks{{"aaa", false}}), sent_data_);
  EXPECT_EQ(1, sent_trailers_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest,
       RequestTrailersNotForwardedAfterTooLargeReplyEndsStreamWithoutGuard) {
  initializeChain(false);
  filters_[0]->decoder_callbacks_->setBufferLimit(2);
  records_[0].decode_data = [this](Buffer::Instance&, bool end_stream) {
    if (end_stream) {
      filters_[0]->decoder_callbacks_->addDecodedTrailers().addCopy(LowerCaseString("added"), "1");
    }
    return FilterDataStatus::Continue;
  };
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  records_[1].encode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };

  filter_manager_->decodeHeaders(*request_headers_, false);
  // Hold response headers at filter 1 so the 413 from requestDataTooLarge() takes the direct
  // local reply path without aborting the decoder filter chain.
  encodeHeaders(false);
  EXPECT_CALL(filter_manager_callbacks_, onRequestDataTooLarge());
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(ContainsHeader(":status", "413"), _));
  EXPECT_CALL(filter_manager_callbacks_, endStream()).WillOnce([this] {
    filter_manager_->destroyFilters();
  });
  decodeData("aaa", true);

  EXPECT_EQ(0, records_[1].decoded_trailers);
  EXPECT_EQ(0, records_[2].decoded_trailers);
}

FilterManagerTest::EncoderTrailerChain
FilterManagerTest::setupEncoderTrailerChain(bool with_middle_filter,
                                            FilterHeadersStatus last_headers_status,
                                            FilterHeadersStatus first_headers_status) {
  EncoderTrailerChain chain;
  if (with_middle_filter) {
    chain.middle_filter = std::make_shared<NiceMock<MockStreamEncoderFilter>>();
  }

  // Registration order is last_filter -> [middle_filter ->] first_filter -> router_filter, so
  // encoder iteration visits first_filter first and last_filter last.
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([this, last_filter = chain.last_filter, middle_filter = chain.middle_filter,
                       first_filter = chain.first_filter, router_filter = chain.router_filter](
                          FilterChainFactoryCallbacks& callbacks) -> bool {
        callbacks.setFilterConfigName("last_filter");
        createEncoderFilterFactoryCb(last_filter)(callbacks);
        if (middle_filter != nullptr) {
          callbacks.setFilterConfigName("middle_filter");
          createEncoderFilterFactoryCb(middle_filter)(callbacks);
        }
        callbacks.setFilterConfigName("first_filter");
        createEncoderFilterFactoryCb(first_filter)(callbacks);
        callbacks.setFilterConfigName("router_filter");
        createDecoderFilterFactoryCb(router_filter)(callbacks);
        return true;
      });

  chain.request_headers = std::make_unique<TestRequestHeaderMapImpl>(
      TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}});
  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(makeOptRef(*chain.request_headers)));

  filter_manager_->createDownstreamFilterChain();
  filter_manager_->requestHeadersInitialized();
  EXPECT_CALL(*chain.router_filter, decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  filter_manager_->decodeHeaders(*chain.request_headers, true);

  EXPECT_CALL(*chain.first_filter, encodeHeaders(_, false)).WillOnce(Return(first_headers_status));
  if (first_headers_status == FilterHeadersStatus::Continue) {
    if (chain.middle_filter != nullptr) {
      EXPECT_CALL(*chain.middle_filter, encodeHeaders(_, false))
          .WillOnce(Return(FilterHeadersStatus::Continue));
    }
    EXPECT_CALL(*chain.last_filter, encodeHeaders(_, false)).WillOnce(Return(last_headers_status));
  }
  chain.router_filter->callbacks_->encodeHeaders(
      std::make_unique<TestResponseHeaderMapImpl>(TestResponseHeaderMapImpl{{":status", "200"}}),
      false, "via_upstream");
  return chain;
}

// Exercises encodeData(..., /*end_stream=*/true) when first_filter adds trailers and
// last_filter returns StopIterationNoBuffer before resuming asynchronously. With the guard
// off, the trailers are dropped and the codec never sees the end of the stream; with it on,
// last_filter receives them and ends the stream on resume.
void FilterManagerTest::runEncodeDataAddTrailersOnStopIterationNoBufferTest(
    bool forward_held_trailers) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.filter_manager_forward_held_trailers",
                               forward_held_trailers ? "true" : "false"}});

  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(/*with_middle_filter=*/false);

  EXPECT_CALL(*chain.first_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.first_filter->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("x-trailer"),
                                                                     "val");
        return FilterDataStatus::Continue;
      });

  std::string captured_data;
  EXPECT_CALL(*chain.last_filter, encodeData(_, false))
      .WillOnce([&](Buffer::Instance& data, bool) -> FilterDataStatus {
        captured_data = data.toString();
        data.drain(data.length());
        return FilterDataStatus::StopIterationNoBuffer;
      });

  if (forward_held_trailers) {
    EXPECT_CALL(*chain.last_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::StopIteration));
  } else {
    EXPECT_CALL(*chain.last_filter, encodeTrailers(_)).Times(0);
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);
    EXPECT_CALL(filter_manager_callbacks_, endStream()).Times(0);
  }

  Buffer::OwnedImpl response_chunk("body");
  chain.router_filter->callbacks_->encodeData(response_chunk, true);
  EXPECT_EQ("body", captured_data);

  if (forward_held_trailers) {
    InSequence s;
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce(
            [](Buffer::Instance& data, bool) { EXPECT_EQ("transcoded_body", data.toString()); });
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());

    Buffer::OwnedImpl transcoded_chunk("transcoded_body");
    chain.last_filter->callbacks_->injectEncodedDataToFilterChain(transcoded_chunk, false);
    chain.last_filter->callbacks_->continueEncoding();
  }

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, EncodeDataAddedTrailersDroppedOnStopIterationWithoutGuard) {
  runEncodeDataAddTrailersOnStopIterationNoBufferTest(/*forward_held_trailers=*/false);
}

TEST_F(FilterManagerTest, EncodeDataAddedTrailersPropagatedOnStopIterationNoBuffer) {
  runEncodeDataAddTrailersOnStopIterationNoBufferTest(/*forward_held_trailers=*/true);
}

// A filter that buffers (StopIterationAndBuffer) still receives trailers added by an earlier filter
// synchronously; its continueEncoding() then flushes the buffered data before the trailers.
TEST_F(FilterManagerTest, EncodeDataAddedTrailersPropagatedOnStopIterationAndBuffer) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(/*with_middle_filter=*/false);

  EXPECT_CALL(*chain.first_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.first_filter->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("x-trailer"),
                                                                     "val");
        return FilterDataStatus::Continue;
      });
  EXPECT_CALL(*chain.last_filter, encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*chain.last_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));

  Buffer::OwnedImpl body("buffered_body");
  chain.router_filter->callbacks_->encodeData(body, true);

  {
    InSequence s;
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce(
            [](Buffer::Instance& data, bool) { EXPECT_EQ("buffered_body", data.toString()); });
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  chain.last_filter->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// When a filter paused in encodeHeaders() returns Continue in encodeData(), commonContinue()
// flushes headers and data while encodeData() propagates added trailers through all intermediate
// filters without skipping or duplicate trailer encoding.
TEST_F(FilterManagerTest, EncodeDataAddedTrailersNotDoubleEncodedOnContinueFromPausedHeaders) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(
      /*with_middle_filter=*/true,
      /*last_headers_status=*/FilterHeadersStatus::StopIteration);

  {
    InSequence s;
    EXPECT_CALL(*chain.first_filter, encodeData(_, true))
        .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
          chain.first_filter->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("x-trailer"),
                                                                       "once");
          return FilterDataStatus::Continue;
        });
    EXPECT_CALL(*chain.middle_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(*chain.last_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) { EXPECT_EQ("payload", data.toString()); });
    EXPECT_CALL(*chain.middle_filter, encodeTrailers(ContainsHeader("x-trailer", "once")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.middle_filter, encodeComplete());
    EXPECT_CALL(*chain.last_filter, encodeTrailers(ContainsHeader("x-trailer", "once")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.last_filter, encodeComplete());
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "once")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }

  Buffer::OwnedImpl payload("payload");
  chain.router_filter->callbacks_->encodeData(payload, true);

  filter_manager_->destroyFilters();
}

// If one filter both calls addEncodedTrailers() and stops iteration in encodeData(),
// continueEncoding() flushes its buffered data before encoding the trailers.
TEST_F(FilterManagerTest, EncodeDataSameFilterAddsTrailersAndStopsIteration) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(/*with_middle_filter=*/false);

  EXPECT_CALL(*chain.first_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.first_filter->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("x-trailer"),
                                                                     "deferred");
        return FilterDataStatus::StopIterationAndBuffer;
      });
  EXPECT_CALL(*chain.last_filter, encodeData(_, _)).Times(0);
  EXPECT_CALL(*chain.last_filter, encodeTrailers(_)).Times(0);

  Buffer::OwnedImpl payload("deferred_payload");
  chain.router_filter->callbacks_->encodeData(payload, true);

  {
    InSequence s;
    EXPECT_CALL(*chain.last_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce(
            [](Buffer::Instance& data, bool) { EXPECT_EQ("deferred_payload", data.toString()); });
    EXPECT_CALL(*chain.last_filter, encodeTrailers(ContainsHeader("x-trailer", "deferred")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "deferred")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  chain.first_filter->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// If a later encoder filter calls sendLocalReply() during encodeData(), newly added
// trailers are not propagated down the aborted encoder chain.
TEST_F(FilterManagerTest, EncodeDataAddedTrailersSkippedWhenLaterFilterSendsLocalReply) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(/*with_middle_filter=*/false);

  EXPECT_CALL(*chain.first_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.first_filter->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("x-trailer"),
                                                                     "aborted");
        return FilterDataStatus::Continue;
      });
  EXPECT_CALL(*chain.last_filter, encodeData(_, false))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.last_filter->callbacks_->sendLocalReply(Code::InternalServerError, "error", nullptr,
                                                      std::nullopt, "encode_data_abort");
        return FilterDataStatus::StopIterationNoBuffer;
      });
  EXPECT_CALL(*chain.last_filter, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, resetStream(_, _));

  Buffer::OwnedImpl payload("payload");
  chain.router_filter->callbacks_->encodeData(payload, true);

  filter_manager_->destroyFilters();
}

// When the filter after the trailer-adding filter stopped all iteration in encodeHeaders(), the
// intermediate filter receives the added trailers right away, while the stopped filter receives
// data and then trailers exactly once when it resumes (from itself, via
// iterate_from_current_filter_).
TEST_F(FilterManagerTest, EncodeDataAddedTrailersPropagatedUpToStopAllFilterThenContinued) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(
      /*with_middle_filter=*/true,
      /*last_headers_status=*/FilterHeadersStatus::StopAllIterationAndBuffer);

  {
    InSequence s;
    EXPECT_CALL(*chain.first_filter, encodeData(_, true))
        .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
          chain.first_filter->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("x-trailer"),
                                                                       "val");
          return FilterDataStatus::Continue;
        });
    EXPECT_CALL(*chain.middle_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(*chain.middle_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.middle_filter, encodeComplete());
  }
  EXPECT_CALL(*chain.last_filter, encodeData(_, _)).Times(0);
  EXPECT_CALL(*chain.last_filter, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, _)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeData(_, _)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);

  Buffer::OwnedImpl payload("payload");
  chain.router_filter->callbacks_->encodeData(payload, true);
  ::testing::Mock::VerifyAndClearExpectations(chain.last_filter.get());
  ::testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  {
    InSequence s;
    EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
    EXPECT_CALL(*chain.last_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) { EXPECT_EQ("payload", data.toString()); });
    EXPECT_CALL(*chain.last_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.last_filter, encodeComplete());
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  chain.last_filter->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// A filter paused in encodeHeaders() with StopAllIteration that, on resume, buffers the body again
// (StopIterationAndBuffer) and only continues from encodeTrailers() must still deliver body then
// trailers once. The resume from encodeTrailers() nests inside the first continueEncoding(), while
// iterate_from_current_filter_ is still set; the nested pass must start at the next filter rather
// than at this one, which has already seen the end of the stream.
TEST_F(FilterManagerTest, EncodeDataStopAllFilterRebuffersBodyThenContinuesFromEncodeTrailers) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(
      /*with_middle_filter=*/false,
      /*last_headers_status=*/FilterHeadersStatus::StopAllIterationAndBuffer);

  EXPECT_CALL(*chain.first_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.first_filter->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("x-trailer"),
                                                                     "val");
        return FilterDataStatus::Continue;
      });
  EXPECT_CALL(*chain.last_filter, encodeData(_, _)).Times(0);
  EXPECT_CALL(*chain.last_filter, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeData(_, _)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);

  Buffer::OwnedImpl payload("payload");
  chain.router_filter->callbacks_->encodeData(payload, true);
  ::testing::Mock::VerifyAndClearExpectations(chain.last_filter.get());
  ::testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  {
    InSequence s;
    EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
    EXPECT_CALL(*chain.last_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
    EXPECT_CALL(*chain.last_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.last_filter, encodeComplete());
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) {
          EXPECT_EQ("payload", data.toString());
          data.drain(data.length());
        });
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  chain.last_filter->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// The first encoder filter adds trailers and stops with StopIterationNoBuffer, then streams
// multiple end_stream=false chunks into a StopAllIterationAndBuffer later filter that resumes after
// the first chunk. The later filter must flush the first chunk while deferring trailers until
// first_filter injects the remaining chunk and calls continueEncoding().
TEST_F(FilterManagerTest, EncodeDataAddedTrailersHolderStreamsMultipleChunksIntoStopAllFilter) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(
      /*with_middle_filter=*/false,
      /*last_headers_status=*/FilterHeadersStatus::StopAllIterationAndBuffer);

  EXPECT_CALL(*chain.first_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance& data, bool) -> FilterDataStatus {
        chain.first_filter->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("x-trailer"),
                                                                     "val");
        data.drain(data.length());
        return FilterDataStatus::StopIterationNoBuffer;
      });
  EXPECT_CALL(*chain.first_filter, encodeComplete());
  Buffer::OwnedImpl input("input");
  chain.router_filter->callbacks_->encodeData(input, true);
  ::testing::Mock::VerifyAndClearExpectations(chain.first_filter.get());

  Buffer::OwnedImpl part1("part1");
  chain.first_filter->callbacks_->injectEncodedDataToFilterChain(part1, false);

  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
  EXPECT_CALL(*chain.last_filter, encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
      .WillOnce([](Buffer::Instance& data, bool) {
        EXPECT_EQ("part1", data.toString());
        data.drain(data.length());
      });
  EXPECT_CALL(*chain.last_filter, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);
  chain.last_filter->callbacks_->continueEncoding();
  ::testing::Mock::VerifyAndClearExpectations(chain.last_filter.get());
  ::testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  const auto empty_buffer =
      testing::Truly([](Buffer::Instance& data) { return data.length() == 0; });
  const auto part2_buffer =
      testing::Truly([](Buffer::Instance& data) { return data.toString() == "part2"; });
  EXPECT_CALL(*chain.last_filter, encodeData(empty_buffer, false))
      .Times(testing::AnyNumber())
      .WillRepeatedly(Return(FilterDataStatus::Continue));
  EXPECT_CALL(filter_manager_callbacks_, encodeData(empty_buffer, false))
      .Times(testing::AnyNumber());
  {
    InSequence s;
    EXPECT_CALL(*chain.last_filter, encodeData(part2_buffer, false))
        .WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(part2_buffer, false))
        .WillOnce([](Buffer::Instance& data, bool) { data.drain(data.length()); });
    EXPECT_CALL(*chain.last_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.last_filter, encodeComplete());
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  Buffer::OwnedImpl part2("part2");
  chain.first_filter->callbacks_->injectEncodedDataToFilterChain(part2, false);
  chain.first_filter->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// The first encoder filter, paused in StopAllIterationAndBuffer, resumes after data and trailers
// arrive from router_filter into a later filter that stops on headers (StopIteration) and buffers
// data (StopIterationAndBuffer) until encodeTrailers(). Because trailers have already reached
// first_filter, its doTrailers() must deliver them to last_filter rather than deferring because
// last_filter stopped on data.
TEST_F(FilterManagerTest, EncodeDataStopAllResumesIntoLaterBufferingFilter) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(
      /*with_middle_filter=*/false,
      /*last_headers_status=*/FilterHeadersStatus::Continue,
      /*first_headers_status=*/FilterHeadersStatus::StopAllIterationAndBuffer);

  Buffer::OwnedImpl payload("payload");
  chain.router_filter->callbacks_->encodeData(payload, false);
  auto trailers = std::make_unique<TestResponseTrailerMapImpl>(
      TestResponseTrailerMapImpl{{"x-trailer", "val"}});
  chain.router_filter->callbacks_->encodeTrailers(std::move(trailers));

  {
    InSequence s;
    EXPECT_CALL(*chain.last_filter, encodeHeaders(_, false))
        .WillOnce(Return(FilterHeadersStatus::StopIteration));
    EXPECT_CALL(*chain.first_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(*chain.last_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
    EXPECT_CALL(*chain.first_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.first_filter, encodeComplete());
    EXPECT_CALL(*chain.last_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.last_filter, encodeComplete());
    EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) {
          EXPECT_EQ("payload", data.toString());
          data.drain(data.length());
        });
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  chain.first_filter->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// A filter paused in encodeHeaders() with StopAllIteration that resumes before an earlier filter
// has released the body must not encode the added trailers from itself (it re-iterates from
// itself, but the earlier stopped filter still owns the stream); that filter's continueEncoding()
// delivers body then trailers exactly once.
TEST_F(FilterManagerTest, EncodeDataAddedTrailersDeferredWhenStopAllFilterResumesFirst) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(
      /*with_middle_filter=*/true,
      /*last_headers_status=*/FilterHeadersStatus::StopAllIterationAndBuffer);

  EXPECT_CALL(*chain.first_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.first_filter->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("x-trailer"),
                                                                     "val");
        return FilterDataStatus::Continue;
      });
  std::string held_body;
  EXPECT_CALL(*chain.middle_filter, encodeData(_, false))
      .WillOnce([&](Buffer::Instance& data, bool) -> FilterDataStatus {
        held_body = data.toString();
        data.drain(data.length());
        return FilterDataStatus::StopIterationNoBuffer;
      });
  EXPECT_CALL(*chain.middle_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  EXPECT_CALL(*chain.last_filter, encodeData(_, _)).Times(0);
  EXPECT_CALL(*chain.last_filter, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, endStream()).Times(0);

  Buffer::OwnedImpl payload("payload");
  chain.router_filter->callbacks_->encodeData(payload, true);
  EXPECT_EQ("payload", held_body);

  // The StopAll filter resumes first: it flushes headers but must not encode the trailers.
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
  chain.last_filter->callbacks_->continueEncoding();
  ::testing::Mock::VerifyAndClearExpectations(chain.last_filter.get());
  ::testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  // The middle filter releases the body and then continues; body precedes trailers at the codec.
  {
    InSequence s;
    EXPECT_CALL(*chain.last_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) { EXPECT_EQ("payload", data.toString()); });
    EXPECT_CALL(*chain.last_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.last_filter, encodeComplete());
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  Buffer::OwnedImpl released(held_body);
  chain.middle_filter->callbacks_->injectEncodedDataToFilterChain(released, false);
  chain.middle_filter->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// Three async filters each hold one frame (StopIterationNoBuffer), the last of them adds trailers,
// and a fourth filter is paused on headers with StopAllIteration. Whatever order they resume in,
// the codec must see all data before the trailers and the trailers exactly once. Here the paused
// filter resumes first (it must defer the trailers: an earlier filter still owns the stream),
// followed by filter_1, filter_2, and filter_3 in encode order.
TEST_F(FilterManagerTest, EncodeDataAddedTrailersOrderedAcrossMultipleStoppedFilters) {
  initialize();

  // Encode order: filter_1 -> filter_2 -> filter_3 -> filter_4 -> codec.
  auto filter_4 = std::make_shared<NiceMock<MockStreamEncoderFilter>>();
  auto filter_3 = std::make_shared<NiceMock<MockStreamEncoderFilter>>();
  auto filter_2 = std::make_shared<NiceMock<MockStreamEncoderFilter>>();
  auto filter_1 = std::make_shared<NiceMock<MockStreamEncoderFilter>>();
  auto router_filter = std::make_shared<NiceMock<MockStreamDecoderFilter>>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        callbacks.setFilterConfigName("filter_4");
        createEncoderFilterFactoryCb(filter_4)(callbacks);
        callbacks.setFilterConfigName("filter_3");
        createEncoderFilterFactoryCb(filter_3)(callbacks);
        callbacks.setFilterConfigName("filter_2");
        createEncoderFilterFactoryCb(filter_2)(callbacks);
        callbacks.setFilterConfigName("filter_1");
        createEncoderFilterFactoryCb(filter_1)(callbacks);
        callbacks.setFilterConfigName("router");
        createDecoderFilterFactoryCb(router_filter)(callbacks);
        return true;
      });
  RequestHeaderMapPtr request_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(makeOptRef(*request_headers)));
  filter_manager_->createDownstreamFilterChain();
  filter_manager_->requestHeadersInitialized();
  EXPECT_CALL(*router_filter, decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  filter_manager_->decodeHeaders(*request_headers, true);

  EXPECT_CALL(*filter_1, encodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter_2, encodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter_3, encodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter_4, encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopAllIterationAndBuffer));
  router_filter->callbacks_->encodeHeaders(
      std::make_unique<TestResponseHeaderMapImpl>(TestResponseHeaderMapImpl{{":status", "200"}}),
      false, "via_upstream");

  // Each stopping filter drains the frame it holds and replays it on resume.
  std::string held_by_3, held_by_2, held_by_1;
  auto hold = [](std::string& held) {
    return [&held](Buffer::Instance& data, bool) -> FilterDataStatus {
      held += data.toString();
      data.drain(data.length());
      return FilterDataStatus::StopIterationNoBuffer;
    };
  };

  // Frame 1: filter_3 stops.
  EXPECT_CALL(*filter_1, encodeData(_, false)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*filter_2, encodeData(_, false)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*filter_3, encodeData(_, false)).WillOnce(hold(held_by_3));
  Buffer::OwnedImpl frame1("abc");
  router_filter->callbacks_->encodeData(frame1, false);
  EXPECT_EQ("abc", held_by_3);

  // Frame 2: filter_2 stops.
  EXPECT_CALL(*filter_1, encodeData(_, false)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*filter_2, encodeData(_, false)).WillOnce(hold(held_by_2));
  Buffer::OwnedImpl frame2("def");
  router_filter->callbacks_->encodeData(frame2, false);
  EXPECT_EQ("def", held_by_2);

  // Frame 3 (end of stream): filter_1 adds trailers and stops.
  EXPECT_CALL(*filter_1, encodeData(_, true))
      .WillOnce([&](Buffer::Instance& data, bool) -> FilterDataStatus {
        filter_1->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("x-trailer"), "val");
        held_by_1 += data.toString();
        data.drain(data.length());
        return FilterDataStatus::StopIterationNoBuffer;
      });
  EXPECT_CALL(*filter_4, encodeData(_, _)).Times(0);
  EXPECT_CALL(*filter_4, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);
  Buffer::OwnedImpl frame3("ghi");
  router_filter->callbacks_->encodeData(frame3, true);
  EXPECT_EQ("ghi", held_by_1);

  // filter_4 resumes first: headers go out, nothing else may (earlier filters still hold body).
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
  EXPECT_CALL(filter_manager_callbacks_, encodeData(_, _)).Times(0);
  filter_4->callbacks_->continueEncoding();
  ::testing::Mock::VerifyAndClearExpectations(filter_4.get());
  ::testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  // filter_1 resumes: replays its frame into filter_2, which holds it, then hands trailers to
  // filter_2.
  EXPECT_CALL(*filter_2, encodeData(_, false)).WillOnce(hold(held_by_2));
  EXPECT_CALL(*filter_2, encodeTrailers(ContainsHeader("x-trailer", "val")))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  Buffer::OwnedImpl replay_1(held_by_1);
  filter_1->callbacks_->injectEncodedDataToFilterChain(replay_1, false);
  filter_1->callbacks_->continueEncoding();
  EXPECT_EQ("defghi", held_by_2);

  // filter_2 resumes: same hand-off to filter_3.
  EXPECT_CALL(*filter_3, encodeData(_, false)).WillOnce(hold(held_by_3));
  EXPECT_CALL(*filter_3, encodeTrailers(ContainsHeader("x-trailer", "val")))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  Buffer::OwnedImpl replay_2(held_by_2);
  filter_2->callbacks_->injectEncodedDataToFilterChain(replay_2, false);
  filter_2->callbacks_->continueEncoding();
  EXPECT_EQ("abcdefghi", held_by_3);

  // filter_3 resumes last: the whole body reaches filter_4 and the codec, then trailers, once.
  {
    InSequence s;
    EXPECT_CALL(*filter_4, encodeData(_, false)).WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) { EXPECT_EQ("abcdefghi", data.toString()); });
    EXPECT_CALL(*filter_4, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*filter_4, encodeComplete());
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  Buffer::OwnedImpl replay_3(held_by_3);
  filter_3->callbacks_->injectEncodedDataToFilterChain(replay_3, false);
  filter_3->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// A filter replaying an earlier frame into a paused StopAllIteration filter must not cause
// that filter to emit trailers ahead of an earlier filter that still holds the final frame.
// Encode order is filter_1 -> filter_2 -> filter_3 -> filter_4: filter_3 holds frame 1,
// filter_2 holds the final frame (after filter_1 added trailers), and filter_4 is paused on
// headers. filter_3 resumes first, then filter_4 (which must still defer the trailers), then
// filter_2; the codec sees both frames and then the trailers once.
TEST_F(FilterManagerTest, EncodeDataAddedTrailersNotStolenByReplayIntoStopAllFilter) {
  initialize();

  auto filter_4 = std::make_shared<NiceMock<MockStreamEncoderFilter>>();
  auto filter_3 = std::make_shared<NiceMock<MockStreamEncoderFilter>>();
  auto filter_2 = std::make_shared<NiceMock<MockStreamEncoderFilter>>();
  auto filter_1 = std::make_shared<NiceMock<MockStreamEncoderFilter>>();
  auto router_filter = std::make_shared<NiceMock<MockStreamDecoderFilter>>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        callbacks.setFilterConfigName("filter_4");
        createEncoderFilterFactoryCb(filter_4)(callbacks);
        callbacks.setFilterConfigName("filter_3");
        createEncoderFilterFactoryCb(filter_3)(callbacks);
        callbacks.setFilterConfigName("filter_2");
        createEncoderFilterFactoryCb(filter_2)(callbacks);
        callbacks.setFilterConfigName("filter_1");
        createEncoderFilterFactoryCb(filter_1)(callbacks);
        callbacks.setFilterConfigName("router");
        createDecoderFilterFactoryCb(router_filter)(callbacks);
        return true;
      });
  RequestHeaderMapPtr request_headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders())
      .WillByDefault(Return(makeOptRef(*request_headers)));
  filter_manager_->createDownstreamFilterChain();
  filter_manager_->requestHeadersInitialized();
  EXPECT_CALL(*router_filter, decodeHeaders(_, true))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  filter_manager_->decodeHeaders(*request_headers, true);

  EXPECT_CALL(*filter_1, encodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter_2, encodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter_3, encodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*filter_4, encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopAllIterationAndBuffer));
  router_filter->callbacks_->encodeHeaders(
      std::make_unique<TestResponseHeaderMapImpl>(TestResponseHeaderMapImpl{{":status", "200"}}),
      false, "via_upstream");

  auto hold = [](std::string& held) {
    return [&held](Buffer::Instance& data, bool) -> FilterDataStatus {
      held += data.toString();
      data.drain(data.length());
      return FilterDataStatus::StopIterationNoBuffer;
    };
  };
  std::string held_by_3, held_by_2;

  // Frame 1: filter_3 holds it.
  EXPECT_CALL(*filter_1, encodeData(_, false)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*filter_2, encodeData(_, false)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*filter_3, encodeData(_, false)).WillOnce(hold(held_by_3));
  Buffer::OwnedImpl frame1("abc");
  router_filter->callbacks_->encodeData(frame1, false);
  EXPECT_EQ("abc", held_by_3);

  // Frame 2 (end of stream): filter_1 adds trailers, filter_2 holds the frame and stops the
  // forwarded trailers.
  EXPECT_CALL(*filter_1, encodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        filter_1->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("x-trailer"), "val");
        return FilterDataStatus::Continue;
      });
  EXPECT_CALL(*filter_2, encodeData(_, false)).WillOnce(hold(held_by_2));
  EXPECT_CALL(*filter_2, encodeTrailers(ContainsHeader("x-trailer", "val")))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  EXPECT_CALL(*filter_4, encodeData(_, _)).Times(0);
  EXPECT_CALL(*filter_4, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);
  Buffer::OwnedImpl frame2("def");
  router_filter->callbacks_->encodeData(frame2, true);
  EXPECT_EQ("def", held_by_2);

  // filter_3 resumes first: its replay of frame 1 is buffered by the paused filter_4.
  // filter_4 must still defer trailers while filter_2 holds frame 2.
  Buffer::OwnedImpl replay1(held_by_3);
  filter_3->callbacks_->injectEncodedDataToFilterChain(replay1, false);
  filter_3->callbacks_->continueEncoding();

  // filter_4 resumes: headers and the buffered frame 1 go out, trailers must still wait.
  {
    InSequence s;
    EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
    EXPECT_CALL(*filter_4, encodeData(_, false)).WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) {
          EXPECT_EQ("abc", data.toString());
          data.drain(data.length()); // The codec consumes the buffered frame.
        });
  }
  filter_4->callbacks_->continueEncoding();
  ::testing::Mock::VerifyAndClearExpectations(filter_4.get());
  ::testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  // filter_2 resumes last: frame 2, then the trailers, exactly once. Its continueEncoding()
  // also replays the (now empty) shared buffer that filter_4 drained above, which is
  // pre-existing FilterManager behavior and carries no bytes.
  {
    InSequence s;
    EXPECT_CALL(*filter_3, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) -> FilterDataStatus {
          EXPECT_EQ("def", data.toString());
          return FilterDataStatus::Continue;
        });
    EXPECT_CALL(*filter_4, encodeData(_, false)).WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) {
          EXPECT_EQ("def", data.toString());
          data.drain(data.length());
        });
    EXPECT_CALL(*filter_3, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) -> FilterDataStatus {
          EXPECT_EQ(0, data.length());
          return FilterDataStatus::Continue;
        });
    EXPECT_CALL(*filter_4, encodeData(_, false)).WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) { EXPECT_EQ(0, data.length()); });
    EXPECT_CALL(*filter_3, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*filter_4, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*filter_4, encodeComplete());
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  Buffer::OwnedImpl replay2(held_by_2);
  filter_2->callbacks_->injectEncodedDataToFilterChain(replay2, false);
  filter_2->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// A filter that saw the final data frame before any trailers existed (end_stream_ already true)
// holds it and later injects it with end_stream=true; the next filter adds trailers on that
// injected frame and the last filter stops. When the first filter then calls continueEncoding(),
// its doTrailers() must not deliver the trailers again: the adder would receive its own trailers
// and a second encodeComplete(), and the last filter a duplicate encodeTrailers().
TEST_F(FilterManagerTest, EncodeDataAddedTrailersNotRedeliveredByFilterThatSawEndStream) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(/*with_middle_filter=*/true);

  // first_filter sees end_stream=true (no trailers yet), holds the frame and stops.
  std::string held;
  EXPECT_CALL(*chain.first_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance& data, bool) -> FilterDataStatus {
        held = data.toString();
        data.drain(data.length());
        return FilterDataStatus::StopIterationNoBuffer;
      });
  EXPECT_CALL(*chain.first_filter, encodeComplete());
  Buffer::OwnedImpl payload("payload");
  chain.router_filter->callbacks_->encodeData(payload, true);
  EXPECT_EQ("payload", held);

  // first_filter injects the frame with end_stream=true: middle_filter adds trailers, last_filter
  // holds the frame and stops the forwarded trailers. Each sees trailers/encodeComplete once.
  std::string held_by_last;
  {
    InSequence s;
    EXPECT_CALL(*chain.middle_filter, encodeData(_, true))
        .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
          chain.middle_filter->callbacks_->addEncodedTrailers().addCopy(
              LowerCaseString("x-trailer"), "val");
          return FilterDataStatus::Continue;
        });
    EXPECT_CALL(*chain.middle_filter, encodeComplete());
    EXPECT_CALL(*chain.last_filter, encodeData(_, false))
        .WillOnce([&](Buffer::Instance& data, bool) -> FilterDataStatus {
          held_by_last = data.toString();
          data.drain(data.length());
          return FilterDataStatus::StopIterationNoBuffer;
        });
    EXPECT_CALL(*chain.last_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::StopIteration));
    EXPECT_CALL(*chain.last_filter, encodeComplete());
  }
  EXPECT_CALL(*chain.middle_filter, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);
  Buffer::OwnedImpl replay(held);
  chain.first_filter->callbacks_->injectEncodedDataToFilterChain(replay, true);
  chain.first_filter->callbacks_->continueEncoding();
  EXPECT_EQ("payload", held_by_last);
  ::testing::Mock::VerifyAndClearExpectations(chain.middle_filter.get());
  ::testing::Mock::VerifyAndClearExpectations(chain.last_filter.get());
  ::testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  // last_filter releases the body and continues: the codec gets the body, then the trailers, once.
  // (Its continueEncoding() may also replay the empty stream-wide buffer created when first_filter
  // first stopped; that pre-existing behavior carries no bytes and is tolerated here.)
  const auto empty_buffer =
      testing::Truly([](Buffer::Instance& data) { return data.length() == 0; });
  const auto payload_buffer =
      testing::Truly([](Buffer::Instance& data) { return data.toString() == "payload"; });
  EXPECT_CALL(filter_manager_callbacks_, encodeData(empty_buffer, false))
      .Times(testing::AnyNumber());
  {
    InSequence s;
    EXPECT_CALL(filter_manager_callbacks_, encodeData(payload_buffer, false));
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  EXPECT_CALL(*chain.last_filter, encodeTrailers(_)).Times(0);
  Buffer::OwnedImpl release(held_by_last);
  chain.last_filter->callbacks_->injectEncodedDataToFilterChain(release, false);
  chain.last_filter->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// When a filter paused in encodeHeaders() sits after the stopping filter and resumes
// asynchronously before it, doTrailers() defers; the stopping filter's later continueEncoding()
// delivers data and then trailers exactly once.
TEST_F(FilterManagerTest, EncodeDataAddedTrailersDeferredWhenPausedFilterResumesFirst) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(
      /*with_middle_filter=*/true,
      /*last_headers_status=*/FilterHeadersStatus::StopIteration);

  EXPECT_CALL(*chain.first_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.first_filter->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("x-trailer"),
                                                                     "val");
        return FilterDataStatus::Continue;
      });
  EXPECT_CALL(*chain.middle_filter, encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*chain.middle_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  EXPECT_CALL(*chain.last_filter, encodeData(_, _)).Times(0);
  EXPECT_CALL(*chain.last_filter, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);

  Buffer::OwnedImpl payload("payload");
  chain.router_filter->callbacks_->encodeData(payload, true);

  // The paused last_filter resumes first: it flushes headers and the shared buffered data
  // but must not encode the trailers it has not been handed yet.
  {
    InSequence s;
    EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) { EXPECT_EQ("payload", data.toString()); });
  }
  chain.last_filter->callbacks_->continueEncoding();
  ::testing::Mock::VerifyAndClearExpectations(chain.last_filter.get());
  ::testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  {
    InSequence s;
    EXPECT_CALL(*chain.last_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.last_filter, encodeComplete());
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  chain.middle_filter->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// A filter that stopped all iteration in encodeHeaders(), then adds trailers in encodeData() when
// it resumes, must not receive its own trailers or a second encodeComplete() when the next filter
// stops: trailer forwarding starts after the filter that added them.
TEST_F(FilterManagerTest, EncodeDataAdderResumingFromStopAllDoesNotReceiveOwnTrailers) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(
      /*with_middle_filter=*/false, /*last_headers_status=*/FilterHeadersStatus::Continue,
      /*first_headers_status=*/FilterHeadersStatus::StopAllIterationAndBuffer);

  // Data is buffered at the stopped first_filter without reaching anyone.
  EXPECT_CALL(*chain.first_filter, encodeData(_, _)).Times(0);
  EXPECT_CALL(*chain.last_filter, encodeData(_, _)).Times(0);
  Buffer::OwnedImpl payload("payload");
  chain.router_filter->callbacks_->encodeData(payload, true);
  ::testing::Mock::VerifyAndClearExpectations(chain.first_filter.get());
  ::testing::Mock::VerifyAndClearExpectations(chain.last_filter.get());

  {
    InSequence s;
    EXPECT_CALL(*chain.last_filter, encodeHeaders(_, false))
        .WillOnce(Return(FilterHeadersStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
    EXPECT_CALL(*chain.first_filter, encodeData(_, true))
        .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
          chain.first_filter->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("x-trailer"),
                                                                       "val");
          return FilterDataStatus::Continue;
        });
    EXPECT_CALL(*chain.first_filter, encodeComplete());
    EXPECT_CALL(*chain.last_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
    EXPECT_CALL(*chain.last_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::StopIteration));
    EXPECT_CALL(*chain.last_filter, encodeComplete());
  }
  EXPECT_CALL(*chain.first_filter, encodeTrailers(_)).Times(0);
  chain.first_filter->callbacks_->continueEncoding();
  ::testing::Mock::VerifyAndClearExpectations(chain.first_filter.get());
  ::testing::Mock::VerifyAndClearExpectations(chain.last_filter.get());

  {
    InSequence s;
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) {
          EXPECT_EQ("payload", data.toString());
          data.drain(data.length());
        });
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  chain.last_filter->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// On the decode path, a filter resuming from StopAllIterationAndBuffer that re-buffers the request
// body (StopIterationAndBuffer) until decodeTrailers() returns Continue must still deliver the
// buffered body then trailers once: commonHandleAfterTrailersCallback() clears
// iterate_from_current_filter_ before the nested commonContinue() so the nested pass starts at the
// next filter rather than re-entering this one (which already has end_stream_ set).
TEST_F(FilterManagerTest, DecodeTrailersStopAllFilterRebuffersBodyThenContinuesFromDecodeTrailers) {
  initialize();

  auto buffer_filter = std::make_shared<NiceMock<MockStreamDecoderFilter>>();
  auto router_filter = std::make_shared<NiceMock<MockStreamDecoderFilter>>();

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        callbacks.setFilterConfigName("buffer_filter");
        createDecoderFilterFactoryCb(buffer_filter)(callbacks);
        callbacks.setFilterConfigName("router_filter");
        createDecoderFilterFactoryCb(router_filter)(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  RequestHeaderMapPtr headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders()).WillByDefault(Return(makeOptRef(*headers)));
  RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"x-trailer", "val"}}};
  ON_CALL(filter_manager_callbacks_, requestTrailers())
      .WillByDefault(Return(makeOptRef(*trailers)));

  EXPECT_CALL(*buffer_filter, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopAllIterationAndBuffer));
  EXPECT_CALL(*router_filter, decodeHeaders(_, _)).Times(0);
  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*headers, false);

  Buffer::OwnedImpl payload("payload");
  filter_manager_->decodeData(payload, false);
  filter_manager_->decodeTrailers(*trailers);

  {
    InSequence s;
    EXPECT_CALL(*router_filter, decodeHeaders(_, false))
        .WillOnce(Return(FilterHeadersStatus::Continue));
    EXPECT_CALL(*buffer_filter, decodeData(_, false))
        .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
    EXPECT_CALL(*buffer_filter, decodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*buffer_filter, decodeComplete());
    EXPECT_CALL(*router_filter, decodeData(_, false)).WillOnce([](Buffer::Instance& data, bool) {
      EXPECT_EQ("payload", data.toString());
      return FilterDataStatus::Continue;
    });
    EXPECT_CALL(*router_filter, decodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*router_filter, decodeComplete());
  }
  buffer_filter->callbacks_->continueDecoding();

  filter_manager_->destroyFilters();
}

// Same decode-path scenario when request trailers were synthesized via addDecodedTrailers() by
// an earlier decoder filter during decodeData(end_stream=true) rather than sent by the client.
TEST_F(FilterManagerTest,
       DecodeDataAddedTrailersStopAllFilterRebuffersBodyThenContinuesFromDecodeTrailers) {
  initialize();

  auto adder_filter = std::make_shared<NiceMock<MockStreamDecoderFilter>>();
  auto buffer_filter = std::make_shared<NiceMock<MockStreamDecoderFilter>>();
  auto router_filter = std::make_shared<NiceMock<MockStreamDecoderFilter>>();

  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillOnce([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        callbacks.setFilterConfigName("adder_filter");
        createDecoderFilterFactoryCb(adder_filter)(callbacks);
        callbacks.setFilterConfigName("buffer_filter");
        createDecoderFilterFactoryCb(buffer_filter)(callbacks);
        callbacks.setFilterConfigName("router_filter");
        createDecoderFilterFactoryCb(router_filter)(callbacks);
        return true;
      });
  filter_manager_->createDownstreamFilterChain();

  RequestHeaderMapPtr headers{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
  ON_CALL(filter_manager_callbacks_, requestHeaders()).WillByDefault(Return(makeOptRef(*headers)));
  RequestTrailerMapPtr saved_trailers;
  EXPECT_CALL(filter_manager_callbacks_, setRequestTrailers(_))
      .WillOnce([&](RequestTrailerMapPtr&& new_trailers) {
        saved_trailers = std::move(new_trailers);
        ON_CALL(filter_manager_callbacks_, requestTrailers())
            .WillByDefault(Return(makeOptRef(*saved_trailers)));
      });

  EXPECT_CALL(*adder_filter, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*buffer_filter, decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopAllIterationAndBuffer));
  EXPECT_CALL(*router_filter, decodeHeaders(_, _)).Times(0);
  filter_manager_->requestHeadersInitialized();
  filter_manager_->decodeHeaders(*headers, false);

  EXPECT_CALL(*adder_filter, decodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        adder_filter->callbacks_->addDecodedTrailers().addCopy(LowerCaseString("x-trailer"), "val");
        return FilterDataStatus::Continue;
      });
  Buffer::OwnedImpl payload("payload");
  filter_manager_->decodeData(payload, true);

  {
    InSequence s;
    EXPECT_CALL(*router_filter, decodeHeaders(_, false))
        .WillOnce(Return(FilterHeadersStatus::Continue));
    EXPECT_CALL(*buffer_filter, decodeData(_, false))
        .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
    EXPECT_CALL(*buffer_filter, decodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*buffer_filter, decodeComplete());
    EXPECT_CALL(*router_filter, decodeData(_, false)).WillOnce([](Buffer::Instance& data, bool) {
      EXPECT_EQ("payload", data.toString());
      return FilterDataStatus::Continue;
    });
    EXPECT_CALL(*router_filter, decodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*router_filter, decodeComplete());
  }
  buffer_filter->callbacks_->continueDecoding();

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, RequestStopAllFilterCanModifyDataParkedAtIt) {
  initializeChain(true);
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopAllIterationAndBuffer; };

  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);
  // Filter 0 let the data through, so the filter it is parked at owns it.
  filters_[1]->decoder_callbacks_->modifyDecodingBuffer([](Buffer::Instance& buffer) {
    buffer.drain(buffer.length());
    buffer.add("AAA");
  });

  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"AAA", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTrailersTest, ResponseStopAllFilterCanModifyDataParkedAtIt) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[1].encode_headers = [](bool) { return FilterHeadersStatus::StopAllIterationAndBuffer; };

  encodeHeaders(false);
  encodeData("aaa", true);
  // Filter 2 let the data through, so the filter it is parked at owns it.
  filters_[1]->encoder_callbacks_->modifyEncodingBuffer([](Buffer::Instance& buffer) {
    buffer.drain(buffer.length());
    buffer.add("AAA");
  });

  filters_[1]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ((Chunks{{"AAA", true}}), sent_data_);

  filter_manager_->destroyFilters();
}

class FilterManagerObservedEndStreamTest : public FilterManagerTest {
public:
  using Chunks = std::vector<std::pair<std::string, bool>>;

  void initializeChain(bool per_filter_buffer) {
    scoped_runtime_.mergeValues({{"envoy.reloadable_features.filter_manager_per_filter_buffer",
                                  per_filter_buffer ? "true" : "false"}});
    initialize();

    for (size_t i = 0; i < filters_.size(); i++) {
      filters_[i] = std::make_shared<NiceMock<MockStreamFilter>>();
      ON_CALL(*filters_[i], decodeHeaders(_, _))
          .WillByDefault(Invoke([this, i](RequestHeaderMap&, bool end_stream) {
            decoded_headers_[i].push_back(end_stream);
            return decode_headers_[i];
          }));
      ON_CALL(*filters_[i], decodeData(_, _))
          .WillByDefault(Invoke([this, i](Buffer::Instance& data, bool end_stream) {
            decoded_data_[i].emplace_back(data.toString(), end_stream);
            // Filter 0 consumes the body to inject it later; the last filter consumes it, as the
            // router does.
            if (i != 1) {
              data.drain(data.length());
              return i == 0 ? FilterDataStatus::StopIterationNoBuffer : FilterDataStatus::Continue;
            }
            return FilterDataStatus::Continue;
          }));
      ON_CALL(*filters_[i], encodeHeaders(_, _))
          .WillByDefault(
              Invoke([this, i](ResponseHeaderMap&, bool) { return encode_headers_[i]; }));
    }

    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillRepeatedly(Invoke([this](FilterChainFactoryCallbacks& callbacks) -> bool {
          constexpr std::array<absl::string_view, 3> names{"filter0", "filter1", "filter2"};
          for (size_t i = 0; i < filters_.size(); i++) {
            callbacks.setFilterConfigName(names[i]);
            createStreamFilterFactoryCb(filters_[i])(callbacks);
          }
          return true;
        }));
    ON_CALL(filter_manager_callbacks_, requestHeaders())
        .WillByDefault(Return(makeOptRef(*request_headers_)));
    ON_CALL(filter_manager_callbacks_, encodeHeaders(_, _))
        .WillByDefault(Invoke(
            [this](ResponseHeaderMap&, bool end_stream) { sent_headers_.push_back(end_stream); }));

    filter_manager_->createDownstreamFilterChain();
    filter_manager_->requestHeadersInitialized();
  }

  void decodeData(absl::string_view body, bool end_stream) {
    Buffer::OwnedImpl data(body);
    filter_manager_->decodeData(data, end_stream);
  }

  // The response is originated by the last decoder filter, i.e. the first encoder filter.
  void encodeHeaders(bool end_stream) {
    filters_[2]->decoder_callbacks_->encodeHeaders(
        std::make_unique<TestResponseHeaderMapImpl>(TestResponseHeaderMapImpl{{":status", "200"}}),
        end_stream, "details");
  }

  // Filter 0 consumes the whole request and filter 1 resumes before filter 0 injects the body.
  void resumeBeforeInjectedBody() {
    decode_headers_[1] = FilterHeadersStatus::StopIteration;
    filter_manager_->decodeHeaders(*request_headers_, false);
    decodeData("aaa", true);
    filters_[1]->decoder_callbacks_->continueDecoding();
  }

  TestScopedRuntime scoped_runtime_;
  std::array<std::shared_ptr<MockStreamFilter>, 3> filters_;
  std::array<FilterHeadersStatus, 3> decode_headers_{
      FilterHeadersStatus::Continue, FilterHeadersStatus::Continue, FilterHeadersStatus::Continue};
  std::array<FilterHeadersStatus, 3> encode_headers_{
      FilterHeadersStatus::Continue, FilterHeadersStatus::Continue, FilterHeadersStatus::Continue};
  std::array<std::vector<bool>, 3> decoded_headers_;
  std::array<Chunks, 3> decoded_data_;
  RequestHeaderMapPtr request_headers_{
      new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "POST"}}};
  std::vector<bool> sent_headers_;
};

TEST_F(FilterManagerObservedEndStreamTest, ResumedFilterDoesNotEndStreamBeforeInjectedBody) {
  initializeChain(true);
  resumeBeforeInjectedBody();
  // The end of stream has not reached filter 1 yet.
  EXPECT_EQ(std::vector<bool>{false}, decoded_headers_[2]);
  EXPECT_TRUE(decoded_data_[2].empty());

  Buffer::OwnedImpl body("aaa");
  filters_[0]->decoder_callbacks_->injectDecodedDataToFilterChain(body, true);
  EXPECT_EQ((Chunks{{"aaa", true}}), decoded_data_[2]);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerObservedEndStreamTest, ResumedFilterEndsStreamEarlyWithoutGuard) {
  initializeChain(false);
  resumeBeforeInjectedBody();
  // The stream-level flag ends the stream at filter 2 before filter 0 injects the body.
  EXPECT_EQ((Chunks{{"", true}}), decoded_data_[2]);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerObservedEndStreamTest, InjectedDecodedHeadersWithEndStreamSetMarker) {
  initializeChain(true);
  decode_headers_[0] = FilterHeadersStatus::StopIteration;
  filter_manager_->decodeHeaders(*request_headers_, false);
  auto* filter = dynamic_cast<ActiveStreamDecoderFilter*>(filters_[0]->decoder_callbacks_);
  ASSERT_NE(nullptr, filter);
  EXPECT_FALSE(filter->observedEndStream());

  filters_[0]->decoder_callbacks_->injectDecodedHeadersToFilterChain(true);
  EXPECT_EQ(std::vector<bool>{true}, decoded_headers_[1]);
  EXPECT_TRUE(filter->observedEndStream());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerObservedEndStreamTest, InjectedEncodedHeadersWithEndStreamSetMarker) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  encode_headers_[2] = FilterHeadersStatus::StopIteration;
  encodeHeaders(false);
  auto* filter = dynamic_cast<ActiveStreamEncoderFilter*>(filters_[2]->encoder_callbacks_);
  ASSERT_NE(nullptr, filter);
  EXPECT_FALSE(filter->observedEndStream());

  filters_[2]->encoder_callbacks_->injectEncodedHeadersToFilterChain(true);
  EXPECT_EQ(std::vector<bool>{true}, sent_headers_);
  EXPECT_TRUE(filter->observedEndStream());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerObservedEndStreamTest, InjectedDecodedDataKeepsStreamEndStream) {
  // The legacy flag would rewrite the stream-level flag; the positional marker ignores it.
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.ext_proc_inject_data_with_state_update", "true"}});
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);
  EXPECT_TRUE(filter_manager_->decoderObservedEndStream());

  Buffer::OwnedImpl chunk1("a1");
  filters_[0]->decoder_callbacks_->injectDecodedDataToFilterChain(chunk1, false);
  EXPECT_TRUE(filter_manager_->decoderObservedEndStream());
  Buffer::OwnedImpl chunk2("a2");
  filters_[0]->decoder_callbacks_->injectDecodedDataToFilterChain(chunk2, true);
  EXPECT_TRUE(filter_manager_->decoderObservedEndStream());
  EXPECT_EQ((Chunks{{"a1", false}, {"a2", true}}), decoded_data_[2]);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerObservedEndStreamTest, HalfCloseKeepsInjectedRequestBodyUntilInjectionEnds) {
  initializeChain(true);
  ON_CALL(filter_manager_callbacks_, isHalfCloseEnabled()).WillByDefault(Return(true));
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);
  Buffer::OwnedImpl chunk1("a1");
  filters_[0]->decoder_callbacks_->injectDecodedDataToFilterChain(chunk1, false);

  // Filter 0 is still injecting, so a complete response does not close the stream yet.
  EXPECT_CALL(filter_manager_callbacks_, endStream()).Times(0);
  encodeHeaders(true);
  testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  EXPECT_CALL(filter_manager_callbacks_, endStream());
  Buffer::OwnedImpl chunk2("a2");
  filters_[0]->decoder_callbacks_->injectDecodedDataToFilterChain(chunk2, true);
  EXPECT_EQ((Chunks{{"a1", false}, {"a2", true}}), decoded_data_[2]);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerObservedEndStreamTest, RecreateStreamRefusedWhileEarlierFilterIsInjecting) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);
  Buffer::OwnedImpl chunk1("a1");
  filters_[0]->decoder_callbacks_->injectDecodedDataToFilterChain(chunk1, false);

  EXPECT_CALL(filter_manager_callbacks_, recreateStream(_)).Times(0);
  EXPECT_FALSE(filters_[2]->decoder_callbacks_->recreateStream(nullptr));

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerObservedEndStreamTest, RecreateStreamAllowedAfterInjectionEnds) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);
  Buffer::OwnedImpl chunk1("a1");
  filters_[0]->decoder_callbacks_->injectDecodedDataToFilterChain(chunk1, false);
  Buffer::OwnedImpl chunk2("a2");
  filters_[0]->decoder_callbacks_->injectDecodedDataToFilterChain(chunk2, true);

  EXPECT_CALL(filter_manager_callbacks_, recreateStream(_));
  EXPECT_TRUE(filters_[2]->decoder_callbacks_->recreateStream(nullptr));

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerObservedEndStreamTest, InjectionWithoutGuardLeavesRecreateToStreamFlag) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.ext_proc_inject_data_with_state_update", "false"}});
  initializeChain(false);
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);
  Buffer::OwnedImpl chunk1("a1");
  filters_[0]->decoder_callbacks_->injectDecodedDataToFilterChain(chunk1, false);

  // Legacy behavior: nothing tracks the pending injection.
  EXPECT_CALL(filter_manager_callbacks_, recreateStream(_));
  EXPECT_TRUE(filters_[2]->decoder_callbacks_->recreateStream(nullptr));

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerObservedEndStreamTest, ResumedEncoderFilterDoesNotEndStreamBeforeInjectedBody) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  // Filter 2, the first encoder filter, consumes the response body to inject it later.
  ON_CALL(*filters_[2], encodeData(_, _)).WillByDefault(Invoke([](Buffer::Instance& data, bool) {
    data.drain(data.length());
    return FilterDataStatus::StopIterationNoBuffer;
  }));
  std::vector<std::pair<std::string, bool>> sent_data;
  ON_CALL(filter_manager_callbacks_, encodeData(_, _))
      .WillByDefault(Invoke([&sent_data](Buffer::Instance& data, bool end_stream) {
        sent_data.emplace_back(data.toString(), end_stream);
        data.drain(data.length());
      }));
  encode_headers_[1] = FilterHeadersStatus::StopIteration;
  encodeHeaders(false);
  Buffer::OwnedImpl response_body("aaa");
  filters_[2]->decoder_callbacks_->encodeData(response_body, true);

  filters_[1]->encoder_callbacks_->continueEncoding();
  // The end of stream has not reached filter 1 yet.
  EXPECT_EQ(std::vector<bool>{false}, sent_headers_);
  EXPECT_TRUE(sent_data.empty());

  Buffer::OwnedImpl body("aaa");
  filters_[2]->encoder_callbacks_->injectEncodedDataToFilterChain(body, true);
  EXPECT_EQ((Chunks{{"aaa", true}}), sent_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerObservedEndStreamTest, RecreateStreamRefusedWhileInjectingByDefault) {
  // Default runtime: the inject guard rewrites the stream-level flag.
  initializeChain(false);
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);
  Buffer::OwnedImpl chunk1("a1");
  filters_[0]->decoder_callbacks_->injectDecodedDataToFilterChain(chunk1, false);

  EXPECT_FALSE(filter_manager_->decoderObservedEndStream());
  EXPECT_CALL(filter_manager_callbacks_, recreateStream(_)).Times(0);
  EXPECT_FALSE(filters_[2]->decoder_callbacks_->recreateStream(nullptr));

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerObservedEndStreamTest, ClientEndStreamAfterInjectionAllowsRecreateStream) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", false);
  Buffer::OwnedImpl chunk1("a1");
  filters_[0]->decoder_callbacks_->injectDecodedDataToFilterChain(chunk1, false);
  EXPECT_TRUE(filter_manager_->decoderInjectionPending());

  // Same as the stream-level flag without per-filter buffer: the client's end of stream wins.
  decodeData("bbb", true);
  EXPECT_FALSE(filter_manager_->decoderInjectionPending());
  EXPECT_CALL(filter_manager_callbacks_, recreateStream(_));
  EXPECT_TRUE(filters_[2]->decoder_callbacks_->recreateStream(nullptr));

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerObservedEndStreamTest, ClientTrailersAfterInjectionAllowRecreateStream) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", false);
  Buffer::OwnedImpl chunk1("a1");
  filters_[0]->decoder_callbacks_->injectDecodedDataToFilterChain(chunk1, false);
  EXPECT_TRUE(filter_manager_->decoderInjectionPending());

  TestRequestTrailerMapImpl trailers{{"foo", "bar"}};
  filter_manager_->decodeTrailers(trailers);
  EXPECT_FALSE(filter_manager_->decoderInjectionPending());
  EXPECT_CALL(filter_manager_callbacks_, recreateStream(_));
  EXPECT_TRUE(filters_[2]->decoder_callbacks_->recreateStream(nullptr));

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest,
       LaterFilterResumingFirstDoesNotSendEarlierHeldRequestBody) {
  initializeChain(true);
  records_[0].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);

  // Filter 1 resumes before filter 0, which still holds the body.
  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ(std::vector<bool>{false}, records_[2].decoded_headers);
  EXPECT_TRUE(records_[2].decoded_data.empty());
  EXPECT_EQ(nullptr, filters_[1]->decoder_callbacks_->decodingBuffer());

  filters_[0]->decoder_callbacks_->modifyDecodingBuffer(
      [](Buffer::Instance& buffer) { buffer.add("!"); });
  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaa!", true}}), records_[1].decoded_data);
  EXPECT_EQ((Chunks{{"aaa!", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, LaterFilterResumingFirstSendsEarlierHeldBodyWhenDisabled) {
  initializeChain(false);
  records_[0].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);

  // Legacy behavior: the body held by filter 0 skips filter 1 and ends the stream.
  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_TRUE(records_[1].decoded_data.empty());
  EXPECT_EQ((Chunks{{"aaa", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest,
       LaterFilterResumingFirstDoesNotSendEarlierHeldResponseBody) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  // Filter 2 is the first encoder filter.
  records_[2].encode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  records_[1].encode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  encodeHeaders(false);
  encodeData("aaa", true);

  filters_[1]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ(std::vector<bool>{false}, sent_headers_);
  EXPECT_TRUE(sent_data_.empty());
  EXPECT_EQ(nullptr, filters_[1]->encoder_callbacks_->encodingBuffer());

  filters_[2]->encoder_callbacks_->modifyEncodingBuffer(
      [](Buffer::Instance& buffer) { buffer.add("!"); });
  filters_[2]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ((Chunks{{"aaa!", true}}), records_[1].encoded_data);
  EXPECT_EQ((Chunks{{"aaa!", true}}), sent_data_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, RouterKeepsRetryBodyWhenInjectionMakesItLatest) {
  initializeChain(true);
  // Filter 0 holds the body and injects it later.
  records_[0].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationNoBuffer;
  };
  // Filter 1 replaces the body and drops the original one.
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[1].decode_data = [](Buffer::Instance& data, bool) {
    data.drain(data.length());
    return FilterDataStatus::Continue;
  };
  // Filter 2 keeps the body for retries, as the router does.
  records_[2].decode_data = [this](Buffer::Instance& data, bool) {
    filters_[2]->decoder_callbacks_->addDecodedData(data, true);
    return FilterDataStatus::StopIterationNoBuffer;
  };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);

  Buffer::OwnedImpl replacement("RRR");
  filters_[1]->decoder_callbacks_->addDecodedData(replacement, false);
  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"RRR", false}}), records_[2].decoded_data);

  Buffer::OwnedImpl chunk("aaa");
  filters_[0]->decoder_callbacks_->injectDecodedDataToFilterChain(chunk, true);
  EXPECT_EQ((Chunks{{"RRR", false}, {"", true}}), records_[2].decoded_data);
  EXPECT_EQ("RRR", filters_[2]->decoder_callbacks_->decodingBuffer()->toString());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, BodyAddedBeforeDataReachedFilterPrecedesEarlierHeldBody) {
  initializeChain(true);
  records_[0].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  records_[1].decode_headers = [](bool) { return FilterHeadersStatus::StopIteration; };
  records_[1].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);

  Buffer::OwnedImpl added("RRR");
  filters_[1]->decoder_callbacks_->addDecodedData(added, false);
  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_TRUE(records_[2].decoded_data.empty());
  // Filter 1 keeps its own buffer, so the data reaching it follows what it already holds.
  EXPECT_EQ("RRRaaa", filters_[1]->decoder_callbacks_->decodingBuffer()->toString());

  filters_[1]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"RRRaaa", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, DroppingKeptBodyReleasesWatermarkHeldByEarlierFilter) {
  initializeChain(true);
  filters_[0]->decoder_callbacks_->setBufferLimit(8);
  int calls = 0;
  // Filter 0 passes the first frame and then waits with a watermark, as lua does in an httpCall.
  records_[0].decode_data = [&calls](Buffer::Instance&, bool) {
    return calls++ == 0 ? FilterDataStatus::Continue : FilterDataStatus::StopIterationAndWatermark;
  };
  // Filter 2 keeps the body for retries and drops it once it gives up, as the router does.
  records_[2].decode_data = [this](Buffer::Instance& data, bool) {
    const Buffer::Instance* kept = filters_[2]->decoder_callbacks_->decodingBuffer();
    if (kept != nullptr && kept->length() + data.length() > 8) {
      filters_[2]->decoder_callbacks_->modifyDecodingBuffer(
          [](Buffer::Instance& buffer) { buffer.drain(buffer.length()); });
      data.drain(data.length());
      return FilterDataStatus::Continue;
    }
    filters_[2]->decoder_callbacks_->addDecodedData(data, true);
    return FilterDataStatus::StopIterationNoBuffer;
  };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("kkkkkk", false);

  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  decodeData("hhhhh", false);
  testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  EXPECT_CALL(filter_manager_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ(0, filters_[2]->decoder_callbacks_->decodingBuffer()->length());

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, AddedReplayedDataIsForwardedOnContinueWithoutOtherGuard) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.filter_manager_forward_added_data_on_continue", "false"}});
  initializeChain(true);
  records_[0].decode_data = [](Buffer::Instance&, bool) {
    return FilterDataStatus::StopIterationAndBuffer;
  };
  // Filter 1 buffers the replayed frame through addDecodedData() and continues.
  records_[1].decode_data = [this](Buffer::Instance& data, bool) {
    filters_[1]->decoder_callbacks_->addDecodedData(data, false);
    return FilterDataStatus::Continue;
  };
  filter_manager_->decodeHeaders(*request_headers_, false);
  decodeData("aaa", true);

  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((Chunks{{"aaa", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest,
       BodyAddedWhileResumingHeadersIsSentWithResumingFilterBody) {
  initializeChain(true);
  records_[0].decode_headers = [this](bool) {
    Buffer::OwnedImpl data("aa");
    filters_[0]->decoder_callbacks_->addDecodedData(data, false);
    return FilterHeadersStatus::StopIteration;
  };
  records_[1].decode_headers = [this](bool) {
    Buffer::OwnedImpl data("bb");
    filters_[1]->decoder_callbacks_->addDecodedData(data, false);
    return FilterHeadersStatus::Continue;
  };

  filter_manager_->decodeHeaders(*request_headers_, true);
  EXPECT_TRUE(records_[1].decoded_headers.empty());

  // Filter 1 adds its body while filter 0 resumes the headers.
  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((std::vector<bool>{false}), records_[1].decoded_headers);
  EXPECT_EQ((std::vector<bool>{false}), records_[2].decoded_headers);
  EXPECT_EQ((Chunks{{"aabb", true}}), records_[1].decoded_data);
  EXPECT_EQ((Chunks{{"aabb", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, BodyAddedWhileResumingHeadersIsSentWithStreamBuffer) {
  initializeChain(false);
  records_[0].decode_headers = [this](bool) {
    Buffer::OwnedImpl data("aa");
    filters_[0]->decoder_callbacks_->addDecodedData(data, false);
    return FilterHeadersStatus::StopIteration;
  };
  records_[1].decode_headers = [this](bool) {
    Buffer::OwnedImpl data("bb");
    filters_[1]->decoder_callbacks_->addDecodedData(data, false);
    return FilterHeadersStatus::Continue;
  };

  filter_manager_->decodeHeaders(*request_headers_, true);
  EXPECT_TRUE(records_[1].decoded_headers.empty());

  // Filter 1 adds its body while filter 0 resumes the headers.
  filters_[0]->decoder_callbacks_->continueDecoding();
  EXPECT_EQ((std::vector<bool>{false}), records_[1].decoded_headers);
  EXPECT_EQ((std::vector<bool>{false}), records_[2].decoded_headers);
  EXPECT_EQ((Chunks{{"aabb", true}}), records_[1].decoded_data);
  EXPECT_EQ((Chunks{{"aabb", true}}), records_[2].decoded_data);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest,
       ResponseBodyAddedWhileResumingHeadersIsSentWithResumingFilterBody) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, true);
  records_[2].encode_headers = [this](bool) {
    Buffer::OwnedImpl data("aa");
    filters_[2]->encoder_callbacks_->addEncodedData(data, false);
    return FilterHeadersStatus::StopIteration;
  };
  records_[1].encode_headers = [this](bool) {
    Buffer::OwnedImpl data("bb");
    filters_[1]->encoder_callbacks_->addEncodedData(data, false);
    return FilterHeadersStatus::Continue;
  };

  encodeHeaders(true);
  EXPECT_TRUE(records_[1].encoded_headers.empty());

  // Filter 1 adds its body while filter 2 resumes the headers.
  filters_[2]->encoder_callbacks_->continueEncoding();
  EXPECT_EQ((std::vector<bool>{false}), sent_headers_);
  EXPECT_EQ((Chunks{{"aabb", true}}), records_[1].encoded_data);
  EXPECT_EQ((Chunks{{"aabb", true}}), sent_data_);

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerPerFilterBufferTest, ModifyBufferWithoutBufferedDataIsEnvoyBug) {
  initializeChain(true);
  filter_manager_->decodeHeaders(*request_headers_, false);
  encodeHeaders(false);

  // Filter 1 holds nothing of its own.
  EXPECT_ENVOY_BUG(filters_[1]->decoder_callbacks_->modifyDecodingBuffer(
                       [](Buffer::Instance& buffer) { buffer.add("!"); }),
                   "modifyDecodingBuffer() called by a filter that holds no buffered data");
  EXPECT_ENVOY_BUG(filters_[1]->encoder_callbacks_->modifyEncodingBuffer(
                       [](Buffer::Instance& buffer) { buffer.add("!"); }),
                   "modifyEncodingBuffer() called by a filter that holds no buffered data");

  filter_manager_->destroyFilters();
}

} // namespace
} // namespace Http
} // namespace Envoy
