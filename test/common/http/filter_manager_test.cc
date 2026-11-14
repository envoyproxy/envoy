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

enum class TrailersStopKind { None, BufferData, ContinueOnData, StopAll, StopTrailers };

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
  constexpr std::array<absl::string_view, 5> kinds{"NoStop", "BufferData", "ContinueOnData",
                                                   "StopAll", "StopTrailers"};
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
              TrailersStopKind::StopAll, TrailersStopKind::StopTrailers}) {
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

} // namespace
} // namespace Http
} // namespace Envoy
