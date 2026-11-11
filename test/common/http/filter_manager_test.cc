#include <memory>
#include <optional>

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

#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Return;

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

  // Encoder chain used by the added-trailer tests. Encode order is upstream_filter,
  // [middle_filter,] downstream_filter, codec; router_filter is the terminal decoder filter.
  struct EncoderTrailerChain {
    std::shared_ptr<NiceMock<MockStreamEncoderFilter>> downstream_filter{
        std::make_shared<NiceMock<MockStreamEncoderFilter>>()};
    std::shared_ptr<NiceMock<MockStreamEncoderFilter>> middle_filter;
    std::shared_ptr<NiceMock<MockStreamEncoderFilter>> upstream_filter{
        std::make_shared<NiceMock<MockStreamEncoderFilter>>()};
    std::shared_ptr<NiceMock<MockStreamDecoderFilter>> router_filter{
        std::make_shared<NiceMock<MockStreamDecoderFilter>>()};
    RequestHeaderMapPtr request_headers;
  };

  // Builds the chain, sends request headers through the router filter, and sends 200 response
  // headers with every encoder filter returning Continue except downstream_filter, which returns
  // `downstream_headers_status`, and upstream_filter, which returns `upstream_headers_status`.
  EncoderTrailerChain setupEncoderTrailerChain(
      bool with_middle_filter,
      FilterHeadersStatus downstream_headers_status = FilterHeadersStatus::Continue,
      FilterHeadersStatus upstream_headers_status = FilterHeadersStatus::Continue);
  void runEncodeDataAddTrailersOnStopIterationNoBufferTest(bool encode_trailers_on_stop);

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

FilterManagerTest::EncoderTrailerChain
FilterManagerTest::setupEncoderTrailerChain(bool with_middle_filter,
                                            FilterHeadersStatus downstream_headers_status,
                                            FilterHeadersStatus upstream_headers_status) {
  EncoderTrailerChain chain;
  if (with_middle_filter) {
    chain.middle_filter = std::make_shared<NiceMock<MockStreamEncoderFilter>>();
  }

  // Registration order is downstream -> [middle ->] upstream -> router, so encoder iteration
  // visits upstream_filter first and downstream_filter last.
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly(
          [this, downstream_filter = chain.downstream_filter, middle_filter = chain.middle_filter,
           upstream_filter = chain.upstream_filter,
           router_filter = chain.router_filter](FilterChainFactoryCallbacks& callbacks) -> bool {
            callbacks.setFilterConfigName("downstream_filter");
            createEncoderFilterFactoryCb(downstream_filter)(callbacks);
            if (middle_filter != nullptr) {
              callbacks.setFilterConfigName("middle_filter");
              createEncoderFilterFactoryCb(middle_filter)(callbacks);
            }
            callbacks.setFilterConfigName("upstream_filter");
            createEncoderFilterFactoryCb(upstream_filter)(callbacks);
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

  EXPECT_CALL(*chain.upstream_filter, encodeHeaders(_, false))
      .WillOnce(Return(upstream_headers_status));
  if (upstream_headers_status == FilterHeadersStatus::Continue) {
    if (chain.middle_filter != nullptr) {
      EXPECT_CALL(*chain.middle_filter, encodeHeaders(_, false))
          .WillOnce(Return(FilterHeadersStatus::Continue));
    }
    EXPECT_CALL(*chain.downstream_filter, encodeHeaders(_, false))
        .WillOnce(Return(downstream_headers_status));
  }
  chain.router_filter->callbacks_->encodeHeaders(
      std::make_unique<TestResponseHeaderMapImpl>(TestResponseHeaderMapImpl{{":status", "200"}}),
      false, "via_upstream");
  return chain;
}

// Exercises encodeData(..., /*end_stream=*/true) when upstream_filter adds trailers and
// downstream_filter returns StopIterationNoBuffer before resuming asynchronously. With the guard
// off, the trailers are dropped and the codec never sees the end of the stream (a stall for any
// filter that finishes streaming responses on encodeTrailers); with it on, downstream_filter
// receives them and ends the stream on resume.
void FilterManagerTest::runEncodeDataAddTrailersOnStopIterationNoBufferTest(
    bool encode_trailers_on_stop) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.filter_manager_encode_trailers_on_stop_iteration",
        encode_trailers_on_stop ? "true" : "false"}});

  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(/*with_middle_filter=*/false);

  EXPECT_CALL(*chain.upstream_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.upstream_filter->callbacks_->addEncodedTrailers().addCopy(
            LowerCaseString("x-trailer"), "val");
        return FilterDataStatus::Continue;
      });

  std::string captured_data;
  EXPECT_CALL(*chain.downstream_filter, encodeData(_, false))
      .WillOnce([&](Buffer::Instance& data, bool) -> FilterDataStatus {
        captured_data = data.toString();
        data.drain(data.length());
        return FilterDataStatus::StopIterationNoBuffer;
      });

  if (encode_trailers_on_stop) {
    EXPECT_CALL(*chain.downstream_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::StopIteration));
  } else {
    EXPECT_CALL(*chain.downstream_filter, encodeTrailers(_)).Times(0);
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);
    EXPECT_CALL(filter_manager_callbacks_, endStream()).Times(0);
  }

  Buffer::OwnedImpl upstream_chunk("body");
  chain.router_filter->callbacks_->encodeData(upstream_chunk, true);
  EXPECT_EQ("body", captured_data);

  if (encode_trailers_on_stop) {
    InSequence s;
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce(
            [](Buffer::Instance& data, bool) { EXPECT_EQ("transcoded_body", data.toString()); });
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());

    Buffer::OwnedImpl transcoded_chunk("transcoded_body");
    chain.downstream_filter->callbacks_->injectEncodedDataToFilterChain(transcoded_chunk, false);
    chain.downstream_filter->callbacks_->continueEncoding();
  }

  filter_manager_->destroyFilters();
}

TEST_F(FilterManagerTest, EncodeDataAddedTrailersDroppedOnStopIterationWithoutGuard) {
  runEncodeDataAddTrailersOnStopIterationNoBufferTest(/*encode_trailers_on_stop=*/false);
}

TEST_F(FilterManagerTest, EncodeDataAddedTrailersPropagatedOnStopIterationNoBuffer) {
  runEncodeDataAddTrailersOnStopIterationNoBufferTest(/*encode_trailers_on_stop=*/true);
}

// A filter that buffers (StopIterationAndBuffer) still receives trailers added by an earlier filter
// synchronously; its continueEncoding() then flushes the buffered data before the trailers.
TEST_F(FilterManagerTest, EncodeDataAddedTrailersPropagatedOnStopIterationAndBuffer) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(/*with_middle_filter=*/false);

  EXPECT_CALL(*chain.upstream_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.upstream_filter->callbacks_->addEncodedTrailers().addCopy(
            LowerCaseString("x-trailer"), "val");
        return FilterDataStatus::Continue;
      });
  EXPECT_CALL(*chain.downstream_filter, encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*chain.downstream_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
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
  chain.downstream_filter->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// When a filter paused in encodeHeaders() returns Continue in encodeData(), commonContinue()
// flushes headers and data while encodeData() propagates added trailers through all intermediate
// filters without skipping or duplicate trailer encoding.
TEST_F(FilterManagerTest, EncodeDataAddedTrailersNotDoubleEncodedOnContinueFromPausedHeaders) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(
      /*with_middle_filter=*/true,
      /*downstream_headers_status=*/FilterHeadersStatus::StopIteration);

  {
    InSequence s;
    EXPECT_CALL(*chain.upstream_filter, encodeData(_, true))
        .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
          chain.upstream_filter->callbacks_->addEncodedTrailers().addCopy(
              LowerCaseString("x-trailer"), "once");
          return FilterDataStatus::Continue;
        });
    EXPECT_CALL(*chain.middle_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(*chain.downstream_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) { EXPECT_EQ("payload", data.toString()); });
    EXPECT_CALL(*chain.middle_filter, encodeTrailers(ContainsHeader("x-trailer", "once")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.middle_filter, encodeComplete());
    EXPECT_CALL(*chain.downstream_filter, encodeTrailers(ContainsHeader("x-trailer", "once")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.downstream_filter, encodeComplete());
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

  EXPECT_CALL(*chain.upstream_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.upstream_filter->callbacks_->addEncodedTrailers().addCopy(
            LowerCaseString("x-trailer"), "deferred");
        return FilterDataStatus::StopIterationAndBuffer;
      });
  EXPECT_CALL(*chain.downstream_filter, encodeData(_, _)).Times(0);
  EXPECT_CALL(*chain.downstream_filter, encodeTrailers(_)).Times(0);

  Buffer::OwnedImpl payload("deferred_payload");
  chain.router_filter->callbacks_->encodeData(payload, true);

  {
    InSequence s;
    EXPECT_CALL(*chain.downstream_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce(
            [](Buffer::Instance& data, bool) { EXPECT_EQ("deferred_payload", data.toString()); });
    EXPECT_CALL(*chain.downstream_filter, encodeTrailers(ContainsHeader("x-trailer", "deferred")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "deferred")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  chain.upstream_filter->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// If a downstream encoder filter calls sendLocalReply() during encodeData(), newly added
// trailers are not propagated down the aborted encoder chain.
TEST_F(FilterManagerTest, EncodeDataAddedTrailersSkippedWhenDownstreamFilterSendsLocalReply) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(/*with_middle_filter=*/false);

  EXPECT_CALL(*chain.upstream_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.upstream_filter->callbacks_->addEncodedTrailers().addCopy(
            LowerCaseString("x-trailer"), "aborted");
        return FilterDataStatus::Continue;
      });
  EXPECT_CALL(*chain.downstream_filter, encodeData(_, false))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.downstream_filter->callbacks_->sendLocalReply(
            Code::InternalServerError, "error", nullptr, std::nullopt, "encode_data_abort");
        return FilterDataStatus::StopIterationNoBuffer;
      });
  EXPECT_CALL(*chain.downstream_filter, encodeTrailers(_)).Times(0);
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
      /*downstream_headers_status=*/FilterHeadersStatus::StopAllIterationAndBuffer);

  {
    InSequence s;
    EXPECT_CALL(*chain.upstream_filter, encodeData(_, true))
        .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
          chain.upstream_filter->callbacks_->addEncodedTrailers().addCopy(
              LowerCaseString("x-trailer"), "val");
          return FilterDataStatus::Continue;
        });
    EXPECT_CALL(*chain.middle_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(*chain.middle_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.middle_filter, encodeComplete());
  }
  EXPECT_CALL(*chain.downstream_filter, encodeData(_, _)).Times(0);
  EXPECT_CALL(*chain.downstream_filter, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, _)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeData(_, _)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);

  Buffer::OwnedImpl payload("payload");
  chain.router_filter->callbacks_->encodeData(payload, true);
  ::testing::Mock::VerifyAndClearExpectations(chain.downstream_filter.get());
  ::testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  {
    InSequence s;
    EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
    EXPECT_CALL(*chain.downstream_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) { EXPECT_EQ("payload", data.toString()); });
    EXPECT_CALL(*chain.downstream_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.downstream_filter, encodeComplete());
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  chain.downstream_filter->callbacks_->continueEncoding();

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
      /*downstream_headers_status=*/FilterHeadersStatus::StopAllIterationAndBuffer);

  EXPECT_CALL(*chain.upstream_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.upstream_filter->callbacks_->addEncodedTrailers().addCopy(
            LowerCaseString("x-trailer"), "val");
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
  EXPECT_CALL(*chain.downstream_filter, encodeData(_, _)).Times(0);
  EXPECT_CALL(*chain.downstream_filter, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, endStream()).Times(0);

  Buffer::OwnedImpl payload("payload");
  chain.router_filter->callbacks_->encodeData(payload, true);
  EXPECT_EQ("payload", held_body);

  // The StopAll filter resumes first: it flushes headers but must not encode the trailers.
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
  chain.downstream_filter->callbacks_->continueEncoding();
  ::testing::Mock::VerifyAndClearExpectations(chain.downstream_filter.get());
  ::testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  // The middle filter releases the body and then continues; body precedes trailers at the codec.
  {
    InSequence s;
    EXPECT_CALL(*chain.downstream_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) { EXPECT_EQ("payload", data.toString()); });
    EXPECT_CALL(*chain.downstream_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.downstream_filter, encodeComplete());
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
// followed by the filters from upstream to downstream.
TEST_F(FilterManagerTest, EncodeDataAddedTrailersOrderedAcrossMultipleStoppedFilters) {
  initialize();

  // Encode order: upstream (D) -> mid_upstream (C) -> mid_downstream (B) -> downstream (A).
  auto downstream = std::make_shared<NiceMock<MockStreamEncoderFilter>>();
  auto mid_downstream = std::make_shared<NiceMock<MockStreamEncoderFilter>>();
  auto mid_upstream = std::make_shared<NiceMock<MockStreamEncoderFilter>>();
  auto upstream = std::make_shared<NiceMock<MockStreamEncoderFilter>>();
  auto router_filter = std::make_shared<NiceMock<MockStreamDecoderFilter>>();
  EXPECT_CALL(filter_factory_, createFilterChain(_))
      .WillRepeatedly([&](FilterChainFactoryCallbacks& callbacks) -> bool {
        callbacks.setFilterConfigName("downstream");
        createEncoderFilterFactoryCb(downstream)(callbacks);
        callbacks.setFilterConfigName("mid_downstream");
        createEncoderFilterFactoryCb(mid_downstream)(callbacks);
        callbacks.setFilterConfigName("mid_upstream");
        createEncoderFilterFactoryCb(mid_upstream)(callbacks);
        callbacks.setFilterConfigName("upstream");
        createEncoderFilterFactoryCb(upstream)(callbacks);
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

  EXPECT_CALL(*upstream, encodeHeaders(_, false)).WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*mid_upstream, encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*mid_downstream, encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(*downstream, encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopAllIterationAndBuffer));
  router_filter->callbacks_->encodeHeaders(
      std::make_unique<TestResponseHeaderMapImpl>(TestResponseHeaderMapImpl{{":status", "200"}}),
      false, "via_upstream");

  // Each stopping filter drains the frame it holds and replays it on resume.
  std::string held_by_b, held_by_c, held_by_d;
  auto hold = [](std::string& held) {
    return [&held](Buffer::Instance& data, bool) -> FilterDataStatus {
      held += data.toString();
      data.drain(data.length());
      return FilterDataStatus::StopIterationNoBuffer;
    };
  };

  // Frame 1: B stops.
  EXPECT_CALL(*upstream, encodeData(_, false)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*mid_upstream, encodeData(_, false)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*mid_downstream, encodeData(_, false)).WillOnce(hold(held_by_b));
  Buffer::OwnedImpl frame1("abc");
  router_filter->callbacks_->encodeData(frame1, false);
  EXPECT_EQ("abc", held_by_b);

  // Frame 2: C stops.
  EXPECT_CALL(*upstream, encodeData(_, false)).WillOnce(Return(FilterDataStatus::Continue));
  EXPECT_CALL(*mid_upstream, encodeData(_, false)).WillOnce(hold(held_by_c));
  Buffer::OwnedImpl frame2("def");
  router_filter->callbacks_->encodeData(frame2, false);
  EXPECT_EQ("def", held_by_c);

  // Frame 3 (end of stream): D adds trailers and stops.
  EXPECT_CALL(*upstream, encodeData(_, true))
      .WillOnce([&](Buffer::Instance& data, bool) -> FilterDataStatus {
        upstream->callbacks_->addEncodedTrailers().addCopy(LowerCaseString("x-trailer"), "val");
        held_by_d += data.toString();
        data.drain(data.length());
        return FilterDataStatus::StopIterationNoBuffer;
      });
  EXPECT_CALL(*downstream, encodeData(_, _)).Times(0);
  EXPECT_CALL(*downstream, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);
  Buffer::OwnedImpl frame3("ghi");
  router_filter->callbacks_->encodeData(frame3, true);
  EXPECT_EQ("ghi", held_by_d);

  // A resumes first: headers go out, nothing else may (D, C and B still own the stream).
  EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
  EXPECT_CALL(filter_manager_callbacks_, encodeData(_, _)).Times(0);
  downstream->callbacks_->continueEncoding();
  ::testing::Mock::VerifyAndClearExpectations(downstream.get());
  ::testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  // D resumes: replays its frame into C, which holds it, then hands the trailers to C.
  EXPECT_CALL(*mid_upstream, encodeData(_, false)).WillOnce(hold(held_by_c));
  EXPECT_CALL(*mid_upstream, encodeTrailers(ContainsHeader("x-trailer", "val")))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  Buffer::OwnedImpl replay_d(held_by_d);
  upstream->callbacks_->injectEncodedDataToFilterChain(replay_d, false);
  upstream->callbacks_->continueEncoding();
  EXPECT_EQ("defghi", held_by_c);

  // C resumes: same hand-off to B.
  EXPECT_CALL(*mid_downstream, encodeData(_, false)).WillOnce(hold(held_by_b));
  EXPECT_CALL(*mid_downstream, encodeTrailers(ContainsHeader("x-trailer", "val")))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  Buffer::OwnedImpl replay_c(held_by_c);
  mid_upstream->callbacks_->injectEncodedDataToFilterChain(replay_c, false);
  mid_upstream->callbacks_->continueEncoding();
  EXPECT_EQ("abcdefghi", held_by_b);

  // B resumes last: the whole body reaches A and the codec, then the trailers, exactly once.
  {
    InSequence s;
    EXPECT_CALL(*downstream, encodeData(_, false)).WillOnce(Return(FilterDataStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) { EXPECT_EQ("abcdefghi", data.toString()); });
    EXPECT_CALL(*downstream, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*downstream, encodeComplete());
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  Buffer::OwnedImpl replay_b(held_by_b);
  mid_downstream->callbacks_->injectEncodedDataToFilterChain(replay_b, false);
  mid_downstream->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

// When a filter paused in encodeHeaders() sits after the stopping filter and resumes
// asynchronously before it, doTrailers() defers; the stopping filter's later continueEncoding()
// delivers data and then trailers exactly once.
TEST_F(FilterManagerTest, EncodeDataAddedTrailersDeferredWhenPausedFilterResumesFirst) {
  initialize();
  EncoderTrailerChain chain = setupEncoderTrailerChain(
      /*with_middle_filter=*/true,
      /*downstream_headers_status=*/FilterHeadersStatus::StopIteration);

  EXPECT_CALL(*chain.upstream_filter, encodeData(_, true))
      .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
        chain.upstream_filter->callbacks_->addEncodedTrailers().addCopy(
            LowerCaseString("x-trailer"), "val");
        return FilterDataStatus::Continue;
      });
  EXPECT_CALL(*chain.middle_filter, encodeData(_, false))
      .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
  EXPECT_CALL(*chain.middle_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
      .WillOnce(Return(FilterTrailersStatus::StopIteration));
  EXPECT_CALL(*chain.downstream_filter, encodeData(_, _)).Times(0);
  EXPECT_CALL(*chain.downstream_filter, encodeTrailers(_)).Times(0);
  EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(_)).Times(0);

  Buffer::OwnedImpl payload("payload");
  chain.router_filter->callbacks_->encodeData(payload, true);

  // The paused downstream filter resumes first: it flushes headers and the shared buffered data
  // but must not encode the trailers it has not been handed yet.
  {
    InSequence s;
    EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) { EXPECT_EQ("payload", data.toString()); });
  }
  chain.downstream_filter->callbacks_->continueEncoding();
  ::testing::Mock::VerifyAndClearExpectations(chain.downstream_filter.get());
  ::testing::Mock::VerifyAndClearExpectations(&filter_manager_callbacks_);

  {
    InSequence s;
    EXPECT_CALL(*chain.downstream_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::Continue));
    EXPECT_CALL(*chain.downstream_filter, encodeComplete());
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
      /*with_middle_filter=*/false, /*downstream_headers_status=*/FilterHeadersStatus::Continue,
      /*upstream_headers_status=*/FilterHeadersStatus::StopAllIterationAndBuffer);

  // Data is buffered at the stopped upstream filter without reaching anyone.
  EXPECT_CALL(*chain.upstream_filter, encodeData(_, _)).Times(0);
  EXPECT_CALL(*chain.downstream_filter, encodeData(_, _)).Times(0);
  Buffer::OwnedImpl payload("payload");
  chain.router_filter->callbacks_->encodeData(payload, true);
  ::testing::Mock::VerifyAndClearExpectations(chain.upstream_filter.get());
  ::testing::Mock::VerifyAndClearExpectations(chain.downstream_filter.get());

  {
    InSequence s;
    EXPECT_CALL(*chain.downstream_filter, encodeHeaders(_, false))
        .WillOnce(Return(FilterHeadersStatus::Continue));
    EXPECT_CALL(filter_manager_callbacks_, encodeHeaders(_, false));
    EXPECT_CALL(*chain.upstream_filter, encodeData(_, true))
        .WillOnce([&](Buffer::Instance&, bool) -> FilterDataStatus {
          chain.upstream_filter->callbacks_->addEncodedTrailers().addCopy(
              LowerCaseString("x-trailer"), "val");
          return FilterDataStatus::Continue;
        });
    EXPECT_CALL(*chain.upstream_filter, encodeComplete());
    EXPECT_CALL(*chain.downstream_filter, encodeData(_, false))
        .WillOnce(Return(FilterDataStatus::StopIterationAndBuffer));
    EXPECT_CALL(*chain.downstream_filter, encodeTrailers(ContainsHeader("x-trailer", "val")))
        .WillOnce(Return(FilterTrailersStatus::StopIteration));
    EXPECT_CALL(*chain.downstream_filter, encodeComplete());
  }
  EXPECT_CALL(*chain.upstream_filter, encodeTrailers(_)).Times(0);
  chain.upstream_filter->callbacks_->continueEncoding();
  ::testing::Mock::VerifyAndClearExpectations(chain.upstream_filter.get());
  ::testing::Mock::VerifyAndClearExpectations(chain.downstream_filter.get());

  {
    InSequence s;
    EXPECT_CALL(filter_manager_callbacks_, encodeData(_, false))
        .WillOnce([](Buffer::Instance& data, bool) { EXPECT_EQ("payload", data.toString()); });
    EXPECT_CALL(filter_manager_callbacks_, encodeTrailers(ContainsHeader("x-trailer", "val")));
    EXPECT_CALL(filter_manager_callbacks_, endStream());
  }
  chain.downstream_filter->callbacks_->continueEncoding();

  filter_manager_->destroyFilters();
}

} // namespace
} // namespace Http
} // namespace Envoy
