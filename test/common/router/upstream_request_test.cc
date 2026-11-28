#include <cstdint>
#include <memory>

#include "envoy/http/codec.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "source/common/common/utility.h"
#include "source/common/network/utility.h"
#include "source/common/router/upstream_codec_filter.h"
#include "source/common/router/upstream_request.h"

#include "test/common/http/common.h"
#include "test/common/memory/memory_test_utility.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/router/router_filter_interface.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

namespace Envoy {
namespace Router {
namespace {

class MockGenericUpstream : public Router::GenericUpstream {
public:
  MockGenericUpstream() { ON_CALL(*this, bytesMeter()).WillByDefault(ReturnRef(bytes_meter_)); }
  MOCK_METHOD(void, encodeData, (Buffer::Instance&, bool), (override));
  MOCK_METHOD(void, encodeMetadata, (const Http::MetadataMapVector&), (override));
  MOCK_METHOD(Http::Status, encodeHeaders, (const Http::RequestHeaderMap&, bool), (override));
  MOCK_METHOD(void, encodeTrailers, (const Http::RequestTrailerMap&), (override));
  MOCK_METHOD(void, enableTcpTunneling, (), (override));
  MOCK_METHOD(void, readDisable, (bool), (override));
  MOCK_METHOD(void, resetStream, (), (override));
  MOCK_METHOD(void, setAccount, (Buffer::BufferMemoryAccountSharedPtr), (override));
  MOCK_METHOD(const StreamInfo::BytesMeterSharedPtr&, bytesMeter, (), (override));

  StreamInfo::BytesMeterSharedPtr bytes_meter_{std::make_shared<StreamInfo::BytesMeter>()};
};

class UpstreamRequestTest : public testing::Test {
public:
  UpstreamRequestTest() : pool_(*symbol_table_) {
    HttpTestUtility::addDefaultHeaders(downstream_request_header_map_);
    ON_CALL(router_filter_interface_, downstreamHeaders())
        .WillByDefault(Return(&downstream_request_header_map_));
    address_ = Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:5678");
    ON_CALL(connection_info_provider_, localAddress()).WillByDefault(ReturnRef(address_));
    ON_CALL(connection_info_provider_, remoteAddress()).WillByDefault(ReturnRef(address_));
  }

  void initialize() {
    auto conn_pool = std::make_unique<NiceMock<Router::MockGenericConnPool>>();
    conn_pool_ = conn_pool.get();
    ON_CALL(*conn_pool_, host()).WillByDefault(Return(host_));
    upstream_request_ =
        std::make_unique<UpstreamRequest>(router_filter_interface_, std::move(conn_pool), false,
                                          true, false /*enable_tcp_tunneling*/);
  }
  Http::FilterFactoryCb createDecoderFilterFactoryCb(Http::StreamDecoderFilterSharedPtr filter) {
    return [filter](Http::FilterChainFactoryCallbacks& callbacks) {
      callbacks.addStreamDecoderFilter(filter);
    };
  }

  void setupUpstreamAndGetBridge(MockGenericUpstream*& mock_upstream, UpstreamToDownstream*& bridge,
                                 std::shared_ptr<UpstreamCodecFilter>* codec_filter = nullptr) {
    std::shared_ptr<Http::MockStreamDecoderFilter> filter(
        new NiceMock<Http::MockStreamDecoderFilter>());

    EXPECT_CALL(*router_filter_interface_.cluster_info_, createFilterChain(_))
        .WillOnce([&](Http::FilterChainFactoryCallbacks& callbacks) -> bool {
          callbacks.setFilterConfigName("");
          callbacks.addStreamDecoderFilter(filter);
          auto f = std::make_shared<UpstreamCodecFilter>();
          if (codec_filter != nullptr) {
            *codec_filter = f;
          }
          callbacks.addStreamDecoderFilter(f);
          return true;
        });

    initialize();

    GenericConnectionPoolCallbacks* pool_callbacks = nullptr;
    EXPECT_CALL(*conn_pool_, newStream(_)).WillOnce([&](GenericConnectionPoolCallbacks* callbacks) {
      pool_callbacks = callbacks;
    });

    upstream_request_->acceptHeadersFromRouter(false);
    ASSERT_NE(pool_callbacks, nullptr);

    auto mock_upstream_unique = std::make_unique<NiceMock<MockGenericUpstream>>();
    mock_upstream = mock_upstream_unique.get();

    pool_callbacks->onPoolReady(std::move(mock_upstream_unique), host_, connection_info_provider_,
                                stream_info_, std::nullopt);

    bridge = &pool_callbacks->upstreamToDownstream();
  }

  Router::MockGenericConnPool* conn_pool_{}; // Owned by the upstream request
  Http::TestRequestHeaderMapImpl downstream_request_header_map_{};
  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::StatNamePool pool_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<MockRouterFilterInterface> router_filter_interface_;
  std::unique_ptr<Router::FilterConfig> router_config_; // must outlive `UpstreamRequest`
  std::unique_ptr<UpstreamRequest> upstream_request_;
  std::shared_ptr<NiceMock<Upstream::MockHostDescription>> host_{
      new NiceMock<Upstream::MockHostDescription>()};
  NiceMock<Network::MockConnectionInfoProvider> connection_info_provider_;
  Network::Address::InstanceConstSharedPtr address_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

// UpstreamRequest is responsible processing for passing 101 upgrade headers to onUpstreamHeaders.
TEST_F(UpstreamRequestTest, Decode101UpgradeHeaders) {
  initialize();

  auto upgrade_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "101"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _));
  upstream_request_->decodeHeaders(std::move(upgrade_headers), false);
}

// UpstreamRequest is responsible for ignoring non-{100,101} 1xx headers.
TEST_F(UpstreamRequestTest, IgnoreOther1xxHeaders) {
  initialize();
  auto other_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "102"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _)).Times(0);
  upstream_request_->decodeHeaders(std::move(other_headers), false);
}

TEST_F(UpstreamRequestTest, TestAccessors) {
  initialize();
  auto response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _));
  upstream_request_->decodeHeaders(std::move(response_headers), false);
}

// UpstreamRequest is responsible for adding proper gRPC annotations to spans.
TEST_F(UpstreamRequestTest, DecodeHeadersGrpcSpanAnnotations) {
  envoy::extensions::filters::http::router::v3::Router router_proto;
  const std::string stats_prefix = "prefix.";
  auto extra_context = Server::Configuration::ExtraFactoryContext::create(context_, stats_prefix);
  router_config_ =
      *Router::FilterConfig::create(context_.server_factory_context_, extra_context,
                                    ShadowWriterPtr(new MockShadowWriter()), router_proto);
  EXPECT_CALL(router_filter_interface_, config()).WillRepeatedly(ReturnRef(*router_config_));

  // Enable tracing in config.
  router_filter_interface_.callbacks_.tracing_config_.spawn_upstream_span_ = true;

  // Set expectations on span.
  auto* child_span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(router_filter_interface_.callbacks_.active_span_, spawnChild_)
      .WillOnce(Return(child_span));
  EXPECT_CALL(*child_span, setTag).Times(AnyNumber());
  EXPECT_CALL(*child_span, setTag(Eq("grpc.status_code"), Eq("1")));
  EXPECT_CALL(*child_span, setTag(Eq("grpc.message"), Eq("failure")));

  // System under test.
  initialize();
  auto upgrade_headers =
      std::make_unique<Http::TestResponseHeaderMapImpl>(Http::TestResponseHeaderMapImpl(
          {{":status", "200"}, {"grpc-status", "1"}, {"grpc-message", "failure"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _));
  upstream_request_->decodeHeaders(std::move(upgrade_headers), false);
}

// UpstreamRequest is responsible for adding proper gRPC annotations to spans.
TEST_F(UpstreamRequestTest,
       DEPRECATED_FEATURE_TEST(DecodeHeadersGrpcSpanAnnotationsWithStartChildSpan)) {
  // Enable tracing in config.
  envoy::extensions::filters::http::router::v3::Router router_proto;
  router_proto.set_start_child_span(true);
  const std::string stats_prefix = "prefix.";
  auto extra_context = Server::Configuration::ExtraFactoryContext::create(context_, stats_prefix);
  router_config_ =
      *Router::FilterConfig::create(context_.server_factory_context_, extra_context,
                                    ShadowWriterPtr(new MockShadowWriter()), router_proto);
  EXPECT_CALL(router_filter_interface_, config()).WillRepeatedly(ReturnRef(*router_config_));

  // Set expectations on span.
  auto* child_span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(router_filter_interface_.callbacks_.active_span_, spawnChild_)
      .WillOnce(Return(child_span));
  EXPECT_CALL(*child_span, setTag).Times(AnyNumber());
  EXPECT_CALL(*child_span, setTag(Eq("grpc.status_code"), Eq("1")));
  EXPECT_CALL(*child_span, setTag(Eq("grpc.message"), Eq("failure")));

  // System under test.
  initialize();
  auto upgrade_headers =
      std::make_unique<Http::TestResponseHeaderMapImpl>(Http::TestResponseHeaderMapImpl(
          {{":status", "200"}, {"grpc-status", "1"}, {"grpc-message", "failure"}}));
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, _));
  upstream_request_->decodeHeaders(std::move(upgrade_headers), false);
}

// Test sending headers from the router to upstream.
TEST_F(UpstreamRequestTest, AcceptRouterHeaders) {
  TestScopedRuntime scoped_runtime;
  std::shared_ptr<Http::MockStreamDecoderFilter> filter(
      new NiceMock<Http::MockStreamDecoderFilter>());

  EXPECT_CALL(*router_filter_interface_.cluster_info_, createFilterChain(_))
      .WillOnce([&](Http::FilterChainFactoryCallbacks& callbacks) -> bool {
        auto factory = createDecoderFilterFactoryCb(filter);
        callbacks.setFilterConfigName("");
        callbacks.addStreamDecoderFilter(filter);
        callbacks.addStreamDecoderFilter(std::make_shared<UpstreamCodecFilter>());
        return true;
      });

  initialize();
  ASSERT_TRUE(filter->callbacks_ != nullptr);

  // Test filter manager accessors.
  EXPECT_FALSE(filter->callbacks_->informationalHeaders().has_value());
  EXPECT_FALSE(filter->callbacks_->responseHeaders().has_value());
  EXPECT_FALSE(filter->callbacks_->http1StreamEncoderOptions().has_value());
  EXPECT_EQ(&filter->callbacks_->tracingConfig().value().get(),
            &router_filter_interface_.callbacks_.tracingConfig().value().get());
  EXPECT_EQ(filter->callbacks_->clusterInfo(), router_filter_interface_.callbacks_.clusterInfo());
  EXPECT_EQ(&filter->callbacks_->activeSpan(), &router_filter_interface_.callbacks_.activeSpan());
  EXPECT_EQ(&filter->callbacks_->streamInfo(), &router_filter_interface_.callbacks_.streamInfo());

  EXPECT_CALL(*conn_pool_, newStream(_)).WillOnce([&](GenericConnectionPoolCallbacks* callbacks) {
    std::stringstream out;
    callbacks->upstreamToDownstream().dumpState(out, 0);
    std::string state = out.str();
    EXPECT_THAT(state, testing::HasSubstr("UpstreamRequest"));
    EXPECT_EQ(callbacks->upstreamToDownstream().connection().ptr(),
              router_filter_interface_.callbacks_.connection().ptr());
    return nullptr;
  });

  EXPECT_CALL(*filter, decodeHeaders(_, false));
  upstream_request_->acceptHeadersFromRouter(false);

  EXPECT_CALL(router_filter_interface_.callbacks_, resetStream(_, _));
  filter->callbacks_->resetStream();
}

TEST_F(UpstreamRequestTest, ConnectionPoolLatencyTime) {
  initialize();

  const auto latency_to_add = std::chrono::microseconds(10);

  EXPECT_CALL(*conn_pool_, newStream(_)).WillOnce([&](GenericConnectionPoolCallbacks* callbacks) {
    router_filter_interface_.callbacks_.dispatcher_.globalTimeSystem().advanceTimeWait(
        latency_to_add);

    callbacks->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                             "Some Failure", nullptr);
    return nullptr;
  });

  upstream_request_->acceptHeadersFromRouter(false);
  const StreamInfo::UpstreamTiming& timing =
      upstream_request_->streamInfo().upstreamInfo()->upstreamTiming();
  ASSERT_TRUE(timing.connectionPoolCallbackLatency().has_value());
  EXPECT_EQ(timing.connectionPoolCallbackLatency().value(), latency_to_add);
}

// UpstreamRequest dumpState without allocating memory.
TEST_F(UpstreamRequestTest, DumpsStateWithoutAllocatingMemory) {
  initialize();
  // Set up router filter
  auto connection_info_provider =
      router_filter_interface_.client_connection_.stream_info_.downstream_connection_info_provider_;
  connection_info_provider->setRemoteAddress(
      Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:5678"));
  connection_info_provider->setLocalAddress(
      Network::Utility::parseInternetAddressAndPortNoThrow("5.6.7.8:5678"));
  connection_info_provider->setDirectRemoteAddressForTest(
      Network::Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:5678"));

  // Dump State
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  Memory::TestUtil::MemoryTest memory_test;
  upstream_request_->dumpState(ostream, 0);
  EXPECT_EQ(memory_test.consumedBytes(), 0);

  // Check Contents
  EXPECT_THAT(ostream.contents(), HasSubstr("UpstreamRequest "));
  EXPECT_THAT(ostream.contents(), HasSubstr("addressProvider: \n  ConnectionInfoSetterImpl "));
  EXPECT_THAT(ostream.contents(), HasSubstr("request_headers: \n"));
}

TEST_F(UpstreamRequestTest, TestSetStreamInfoFields) {
  initialize();
  EXPECT_EQ(upstream_request_->streamInfo().route(), router_filter_interface_.callbacks_.route());
}

TEST_F(UpstreamRequestTest, TestSetStreamInfoHost) {
  initialize();
  EXPECT_EQ(upstream_request_->streamInfo().upstreamInfo()->upstreamHost(), host_);
}

// A simple upstream filter that registers itself as an UpstreamCallbacks.
class TestUpstreamFilter : public Http::StreamDecoderFilter, public Http::UpstreamCallbacks {
public:
  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    callbacks.upstreamCallbacks()->addUpstreamCallbacks(*this);
  }
  void onDestroy() override {}

  // Http::UpstreamCallbacks
  void onHostSelected(const Upstream::HostDescriptionConstSharedPtr& host) override {
    on_host_selected_called_ = true;
    last_host_ = host;
    if (reject_connection_) {
      callbacks_->sendLocalReply(Http::Code::Forbidden, "Connection rejected", nullptr,
                                 std::nullopt, "upstream_filter_rejection");
    }
  }
  void onUpstreamConnectionEstablished() override {}

  Http::StreamDecoderFilterCallbacks* callbacks_{};
  bool on_host_selected_called_{false};
  bool reject_connection_{false};
  Upstream::HostDescriptionConstSharedPtr last_host_;
};

TEST_F(UpstreamRequestTest, OnHostSelectedCallbackInvoked) {
  auto filter = std::make_shared<TestUpstreamFilter>();

  EXPECT_CALL(*router_filter_interface_.cluster_info_, createFilterChain(_))
      .WillOnce([&](Http::FilterChainFactoryCallbacks& callbacks) -> bool {
        callbacks.addStreamDecoderFilter(filter);
        callbacks.addStreamDecoderFilter(std::make_shared<UpstreamCodecFilter>());
        return true;
      });

  initialize();

  EXPECT_CALL(*conn_pool_, newStream(_));
  upstream_request_->acceptHeadersFromRouter(false);

  EXPECT_TRUE(filter->on_host_selected_called_);
  EXPECT_EQ(filter->last_host_, host_);
}

TEST_F(UpstreamRequestTest, OnHostSelectedRejectionViaSendLocalReply) {
  auto filter = std::make_shared<TestUpstreamFilter>();
  filter->reject_connection_ = true;

  EXPECT_CALL(*router_filter_interface_.cluster_info_, createFilterChain(_))
      .WillOnce([&](Http::FilterChainFactoryCallbacks& callbacks) -> bool {
        callbacks.addStreamDecoderFilter(filter);
        callbacks.addStreamDecoderFilter(std::make_shared<UpstreamCodecFilter>());
        return true;
      });

  initialize();

  EXPECT_CALL(*conn_pool_, newStream(_)).Times(0);
  EXPECT_CALL(router_filter_interface_.callbacks_,
              sendLocalReply(Http::Code::Forbidden, _, _, _, _));
  upstream_request_->acceptHeadersFromRouter(false);

  EXPECT_TRUE(filter->on_host_selected_called_);
}

TEST_F(UpstreamRequestTest, OnHostSelectedMultipleCallbacksStopsAfterSendLocalReply) {
  auto filter1 = std::make_shared<TestUpstreamFilter>();
  filter1->reject_connection_ = true; // First filter rejects via sendLocalReply

  auto filter2 = std::make_shared<TestUpstreamFilter>();

  EXPECT_CALL(*router_filter_interface_.cluster_info_, createFilterChain(_))
      .WillOnce([&](Http::FilterChainFactoryCallbacks& callbacks) -> bool {
        callbacks.addStreamDecoderFilter(filter1);
        callbacks.addStreamDecoderFilter(filter2);
        callbacks.addStreamDecoderFilter(std::make_shared<UpstreamCodecFilter>());
        return true;
      });

  initialize();

  EXPECT_CALL(*conn_pool_, newStream(_)).Times(0);
  EXPECT_CALL(router_filter_interface_.callbacks_,
              sendLocalReply(Http::Code::Forbidden, _, _, _, _));
  upstream_request_->acceptHeadersFromRouter(false);

  EXPECT_TRUE(filter1->on_host_selected_called_);
  EXPECT_FALSE(filter2->on_host_selected_called_);
}

TEST_F(UpstreamRequestTest, ReentrantResetDuringDecodeData) {
  MockGenericUpstream* mock_upstream_ptr = nullptr;
  UpstreamToDownstream* bridge = nullptr;
  std::shared_ptr<UpstreamCodecFilter> codec_filter;
  setupUpstreamAndGetBridge(mock_upstream_ptr, bridge, &codec_filter);

  EXPECT_CALL(*mock_upstream_ptr, encodeData(_, _))
      .WillOnce([&, bridge, mock_upstream_ptr](Buffer::Instance&, bool) {
        bridge->onResetStream(Http::StreamResetReason::LocalReset, "test_failure");
        mock_upstream_ptr->bytes_meter_ = nullptr;
      });

  EXPECT_CALL(router_filter_interface_, onUpstreamReset(Http::StreamResetReason::LocalReset,
                                                        HasSubstr("test_failure|codec_error"), _));

  Buffer::OwnedImpl data("hello");
  Http::FilterDataStatus status = codec_filter->decodeData(data, false);
  EXPECT_EQ(status, Http::FilterDataStatus::StopIterationNoBuffer);
  upstream_request_.reset();
}

TEST_F(UpstreamRequestTest, ReentrantResetDuringDecodeTrailers) {
  MockGenericUpstream* mock_upstream_ptr = nullptr;
  UpstreamToDownstream* bridge = nullptr;
  std::shared_ptr<UpstreamCodecFilter> codec_filter;
  setupUpstreamAndGetBridge(mock_upstream_ptr, bridge, &codec_filter);

  EXPECT_CALL(*mock_upstream_ptr, encodeTrailers(_))
      .WillOnce([&, bridge, mock_upstream_ptr](const Http::RequestTrailerMap&) {
        bridge->onResetStream(Http::StreamResetReason::LocalReset, "test_failure");
        mock_upstream_ptr->bytes_meter_ = nullptr;
      });

  EXPECT_CALL(router_filter_interface_, onUpstreamReset(Http::StreamResetReason::LocalReset,
                                                        HasSubstr("test_failure|codec_error"), _));

  Http::TestRequestTrailerMapImpl trailers;
  Http::FilterTrailersStatus status = codec_filter->decodeTrailers(trailers);
  EXPECT_EQ(status, Http::FilterTrailersStatus::StopIteration);
  upstream_request_.reset();
}

TEST_F(UpstreamRequestTest, ReentrantResetDuringDecodeMetadata) {
  MockGenericUpstream* mock_upstream_ptr = nullptr;
  UpstreamToDownstream* bridge = nullptr;
  std::shared_ptr<UpstreamCodecFilter> codec_filter;
  setupUpstreamAndGetBridge(mock_upstream_ptr, bridge, &codec_filter);

  EXPECT_CALL(*mock_upstream_ptr, encodeMetadata(_))
      .WillOnce([&, bridge, mock_upstream_ptr](const Http::MetadataMapVector&) {
        bridge->onResetStream(Http::StreamResetReason::LocalReset, "test_failure");
        mock_upstream_ptr->bytes_meter_ = nullptr;
      });

  EXPECT_CALL(router_filter_interface_, onUpstreamReset(Http::StreamResetReason::LocalReset,
                                                        HasSubstr("test_failure|codec_error"), _));

  Http::MetadataMap metadata;
  Http::FilterMetadataStatus status = codec_filter->decodeMetadata(metadata);
  EXPECT_EQ(status, Http::FilterMetadataStatus::StopIterationForLocalReply);
  upstream_request_.reset();
}

// If the upstream request is destroyed while its codec stream is still live (for example, an
// upstream HTTP filter answered the request itself, so the codec never saw end of stream), the
// codec filter must reset the stream. Otherwise the codec client keeps the stream, and its
// callbacks still point at the destroyed GenericUpstream.
TEST_F(UpstreamRequestTest, UnfinishedCodecStreamIsResetOnDestroy) {
  MockGenericUpstream* mock_upstream_ptr = nullptr;
  UpstreamToDownstream* bridge = nullptr;
  setupUpstreamAndGetBridge(mock_upstream_ptr, bridge);

  EXPECT_CALL(*mock_upstream_ptr, resetStream());
  upstream_request_.reset();
}

// Once the codec has delivered the end of the response, the stream belongs to the codec again and
// may already be freed, so destroying the upstream request must not touch it.
TEST_F(UpstreamRequestTest, CompletedCodecStreamIsNotResetOnDestroy) {
  MockGenericUpstream* mock_upstream_ptr = nullptr;
  UpstreamToDownstream* bridge = nullptr;
  setupUpstreamAndGetBridge(mock_upstream_ptr, bridge);

  bridge->decodeHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
                            Http::TestResponseHeaderMapImpl({{":status", "200"}})),
                        true);

  EXPECT_CALL(*mock_upstream_ptr, resetStream()).Times(0);
  upstream_request_.reset();
}

// Upstream filter helper for testing request- and response-direction watermark propagation.
class WatermarkTestUpstreamFilter : public Http::StreamFilter {
public:
  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override {
    if (raise_decoder_watermark_on_headers_) {
      decoder_callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
    }
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus decodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap&) override {
    return Http::Filter1xxHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus encodeData(Buffer::Instance&, bool) override {
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

  void onDestroy() override {
    if (lower_decoder_watermark_on_destroy_) {
      decoder_callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
    }
    if (upstream_wm_cb_ != nullptr) {
      decoder_callbacks_->removeUpstreamWatermarkCallbacks(*upstream_wm_cb_);
      upstream_wm_cb_ = nullptr;
    }
    if (downstream_wm_cb_ != nullptr) {
      decoder_callbacks_->removeDownstreamWatermarkCallbacks(*downstream_wm_cb_);
      downstream_wm_cb_ = nullptr;
    }
  }

  void subscribeUpstreamWatermark(Http::UpstreamWatermarkCallbacks& cb) {
    upstream_wm_cb_ = &cb;
    decoder_callbacks_->addUpstreamWatermarkCallbacks(cb);
  }
  void subscribeDownstreamWatermark(Http::DownstreamWatermarkCallbacks& cb) {
    downstream_wm_cb_ = &cb;
    decoder_callbacks_->addDownstreamWatermarkCallbacks(cb);
  }

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{nullptr};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{nullptr};
  Http::UpstreamWatermarkCallbacks* upstream_wm_cb_{nullptr};
  Http::DownstreamWatermarkCallbacks* downstream_wm_cb_{nullptr};
  bool raise_decoder_watermark_on_headers_{false};
  bool lower_decoder_watermark_on_destroy_{false};
};

class UpstreamRequestWatermarkTest : public UpstreamRequestTest {
public:
  void initializeWithWatermarkFilter(bool add_second_filter = false) {
    router_filter_interface_.callbacks_.cluster_info_ = router_filter_interface_.cluster_info_;
    filter_ = std::make_shared<WatermarkTestUpstreamFilter>();
    if (add_second_filter) {
      second_filter_ = std::make_shared<WatermarkTestUpstreamFilter>();
    }
    EXPECT_CALL(*router_filter_interface_.cluster_info_, createFilterChain(_))
        .WillOnce([&](Http::FilterChainFactoryCallbacks& callbacks) -> bool {
          callbacks.addStreamFilter(filter_);
          if (second_filter_ != nullptr) {
            callbacks.addStreamFilter(second_filter_);
          }
          callbacks.addStreamDecoderFilter(std::make_shared<UpstreamCodecFilter>());
          return true;
        });
    ON_CALL(router_filter_interface_.callbacks_, addDownstreamWatermarkCallbacks(_))
        .WillByDefault([&](Http::DownstreamWatermarkCallbacks& cb) { router_ds_wm_cb_ = &cb; });
    initialize();
  }

  void startAndEstablishPool() {
    GenericConnectionPoolCallbacks* pool_callbacks = nullptr;
    EXPECT_CALL(*conn_pool_, newStream(_)).WillOnce([&](GenericConnectionPoolCallbacks* callbacks) {
      pool_callbacks = callbacks;
    });
    upstream_request_->acceptHeadersFromRouter(false);
    ASSERT_NE(pool_callbacks, nullptr);
    auto mock_upstream = std::make_unique<NiceMock<MockGenericUpstream>>();
    mock_upstream_ = mock_upstream.get();
    pool_callbacks->onPoolReady(std::move(mock_upstream), host_, connection_info_provider_,
                                stream_info_, Http::Protocol::Http2);
    bridge_ = &pool_callbacks->upstreamToDownstream();
  }

  uint64_t clusterCounter(const std::string& name) {
    return router_filter_interface_.cluster_info_->stats_store_.counter(name).value();
  }

  std::shared_ptr<WatermarkTestUpstreamFilter> filter_;
  std::shared_ptr<WatermarkTestUpstreamFilter> second_filter_;
  MockGenericUpstream* mock_upstream_{nullptr};
  UpstreamToDownstream* bridge_{nullptr};
  Http::DownstreamWatermarkCallbacks* router_ds_wm_cb_{nullptr};
};

// Request-direction high/low watermarks propagate to both downstream callbacks and upstream
// filters subscribed via addUpstreamWatermarkCallbacks(), and late subscribers catch up.
TEST_F(UpstreamRequestWatermarkTest, UpstreamWatermarkCallbacksForwardedAndLateSubscriberCaughtUp) {
  initializeWithWatermarkFilter();
  startAndEstablishPool();

  testing::StrictMock<Http::MockUpstreamWatermarkCallbacks> upstream_wm_cb;
  filter_->subscribeUpstreamWatermark(upstream_wm_cb);

  EXPECT_CALL(router_filter_interface_.callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  EXPECT_CALL(upstream_wm_cb, onAboveWriteBufferHighWatermark());
  bridge_->onAboveWriteBufferHighWatermark();
  EXPECT_EQ(1U, clusterCounter("upstream_flow_control_backed_up_total"));

  testing::StrictMock<Http::MockUpstreamWatermarkCallbacks> late_subscriber;
  EXPECT_CALL(late_subscriber, onAboveWriteBufferHighWatermark());
  filter_->decoder_callbacks_->addUpstreamWatermarkCallbacks(late_subscriber);

  EXPECT_CALL(router_filter_interface_.callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  EXPECT_CALL(upstream_wm_cb, onBelowWriteBufferLowWatermark());
  EXPECT_CALL(late_subscriber, onBelowWriteBufferLowWatermark());
  bridge_->onBelowWriteBufferLowWatermark();
  EXPECT_EQ(1U, clusterCounter("upstream_flow_control_drained_total"));

  filter_->decoder_callbacks_->removeUpstreamWatermarkCallbacks(late_subscriber);
  upstream_request_.reset();
}

// Resetting/cleaning up an UpstreamRequest while above high watermark unwinds downstream
// callbacks cleanly without leaking depth.
TEST_F(UpstreamRequestWatermarkTest, UpstreamWatermarkUnwoundOnCleanUp) {
  initializeWithWatermarkFilter();
  startAndEstablishPool();

  testing::StrictMock<Http::MockUpstreamWatermarkCallbacks> upstream_wm_cb;
  filter_->subscribeUpstreamWatermark(upstream_wm_cb);

  EXPECT_CALL(router_filter_interface_.callbacks_, onDecoderFilterAboveWriteBufferHighWatermark())
      .Times(2);
  EXPECT_CALL(upstream_wm_cb, onAboveWriteBufferHighWatermark()).Times(2);
  bridge_->onAboveWriteBufferHighWatermark();
  bridge_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(router_filter_interface_.callbacks_, onDecoderFilterBelowWriteBufferLowWatermark())
      .Times(2);
  upstream_request_.reset();
  EXPECT_EQ(2U, clusterCounter("upstream_flow_control_drained_total"));
}

// An upstream filter that lowers its own decoder watermark inside onDestroy() must not trigger a
// double-unwind or assert in UpstreamRequest::cleanUp().
TEST_F(UpstreamRequestWatermarkTest, UpstreamWatermarkFilterLowInOnDestroyDoesNotDoubleUnwind) {
  initializeWithWatermarkFilter();
  filter_->raise_decoder_watermark_on_headers_ = true;
  filter_->lower_decoder_watermark_on_destroy_ = true;

  EXPECT_CALL(*conn_pool_, newStream(_));
  EXPECT_CALL(router_filter_interface_.callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  upstream_request_->acceptHeadersFromRouter(false);

  EXPECT_CALL(router_filter_interface_.callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  upstream_request_.reset();
  EXPECT_EQ(1U, clusterCounter("upstream_flow_control_drained_total"));
}

// Reentrant cleanUp() during onDecoderFilterAboveWriteBufferHighWatermark() combined with a filter
// lowering its decoder watermark in onDestroy() must not underflow upstream_high_watermark_count_.
TEST_F(UpstreamRequestWatermarkTest, ReentrantCleanUpDuringUpstreamHighWatermarkDoesNotCrash) {
  initializeWithWatermarkFilter();
  filter_->raise_decoder_watermark_on_headers_ = true;
  filter_->lower_decoder_watermark_on_destroy_ = true;

  EXPECT_CALL(*conn_pool_, newStream(_));
  EXPECT_CALL(router_filter_interface_.callbacks_, onDecoderFilterAboveWriteBufferHighWatermark())
      .WillOnce([&] { upstream_request_->cleanUp(); });
  EXPECT_CALL(router_filter_interface_.callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  upstream_request_->acceptHeadersFromRouter(false);
  upstream_request_.reset();
  EXPECT_EQ(1U, clusterCounter("upstream_flow_control_drained_total"));
}

// Reentrant enableDataFromDownstreamForFlowControl() inside the downstream callback of
// disableDataFromDownstreamForFlowControl() must not leak an unmatched upstream high watermark.
TEST_F(UpstreamRequestWatermarkTest,
       ReentrantLowWatermarkDuringDisableDataDoesNotLeakUpstreamWatermark) {
  initializeWithWatermarkFilter();
  startAndEstablishPool();

  int high_count = 0;
  int low_count = 0;
  testing::NiceMock<Http::MockUpstreamWatermarkCallbacks> upstream_wm_cb;
  ON_CALL(upstream_wm_cb, onAboveWriteBufferHighWatermark()).WillByDefault([&] { ++high_count; });
  ON_CALL(upstream_wm_cb, onBelowWriteBufferLowWatermark()).WillByDefault([&] { ++low_count; });
  filter_->subscribeUpstreamWatermark(upstream_wm_cb);

  EXPECT_CALL(router_filter_interface_.callbacks_, onDecoderFilterAboveWriteBufferHighWatermark())
      .WillOnce([&] { bridge_->onBelowWriteBufferLowWatermark(); });
  EXPECT_CALL(router_filter_interface_.callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());

  bridge_->onAboveWriteBufferHighWatermark();
  EXPECT_EQ(high_count, low_count);
  upstream_request_.reset();
}

// During cleanUp() -> destroyFilters(), a filter lowering its decoder watermark in onDestroy()
// must not deliver live onBelowWriteBufferLowWatermark() callbacks to sibling filters.
TEST_F(UpstreamRequestWatermarkTest, OnDestroyFilterLowWatermarkDoesNotNotifySiblingFilter) {
  initializeWithWatermarkFilter(/*add_second_filter=*/true);
  filter_->raise_decoder_watermark_on_headers_ = true;
  filter_->lower_decoder_watermark_on_destroy_ = true;

  testing::StrictMock<Http::MockUpstreamWatermarkCallbacks> sibling_cb;
  second_filter_->subscribeUpstreamWatermark(sibling_cb);

  EXPECT_CALL(*conn_pool_, newStream(_));
  EXPECT_CALL(router_filter_interface_.callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  EXPECT_CALL(sibling_cb, onAboveWriteBufferHighWatermark());
  upstream_request_->acceptHeadersFromRouter(false);

  EXPECT_CALL(router_filter_interface_.callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());
  EXPECT_CALL(sibling_cb, onBelowWriteBufferLowWatermark()).Times(0);
  upstream_request_.reset();
}

// Downstream backpressure after response start propagates to upstream filters subscribed via
// addDownstreamWatermarkCallbacks() while calling upstream->readDisable() and charging stats
// exactly once (not doubled between UpstreamRequest and UpstreamCodecFilter).
TEST_F(UpstreamRequestWatermarkTest, DownstreamWatermarkForwardedOnceWithoutDoubleReadDisable) {
  initializeWithWatermarkFilter();
  startAndEstablishPool();
  ASSERT_NE(router_ds_wm_cb_, nullptr);

  testing::StrictMock<Http::MockDownstreamWatermarkCallbacks> upstream_filter_ds_cb;
  filter_->subscribeDownstreamWatermark(upstream_filter_ds_cb);

  ON_CALL(router_filter_interface_, downstreamResponseStarted()).WillByDefault(Return(true));
  auto response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  bridge_->decodeHeaders(std::move(response_headers), false);

  EXPECT_CALL(*mock_upstream_, readDisable(true));
  EXPECT_CALL(upstream_filter_ds_cb, onAboveWriteBufferHighWatermark());
  router_ds_wm_cb_->onAboveWriteBufferHighWatermark();
  EXPECT_EQ(1U, clusterCounter("upstream_flow_control_paused_reading_total"));

  EXPECT_CALL(*mock_upstream_, readDisable(false));
  EXPECT_CALL(upstream_filter_ds_cb, onBelowWriteBufferLowWatermark());
  router_ds_wm_cb_->onBelowWriteBufferLowWatermark();
  EXPECT_EQ(1U, clusterCounter("upstream_flow_control_resumed_reading_total"));

  upstream_request_.reset();
}

// Pre-response downstream watermarks are deferred until Router::Filter commits the response,
// cancel out if downstream drains before response headers, and flush once to both
// UpstreamCodecFilter and upstream filter subscribers on commit.
TEST_F(UpstreamRequestWatermarkTest, DownstreamWatermarkDeferredBeforeResponseHeadersAndFlushed) {
  initializeWithWatermarkFilter();
  startAndEstablishPool();
  ASSERT_NE(router_ds_wm_cb_, nullptr);

  testing::StrictMock<Http::MockDownstreamWatermarkCallbacks> upstream_filter_ds_cb;
  filter_->subscribeDownstreamWatermark(upstream_filter_ds_cb);

  ON_CALL(router_filter_interface_, downstreamResponseStarted()).WillByDefault(Return(false));
  EXPECT_CALL(*mock_upstream_, readDisable(_)).Times(0);
  router_ds_wm_cb_->onAboveWriteBufferHighWatermark();
  router_ds_wm_cb_->onAboveWriteBufferHighWatermark();
  router_ds_wm_cb_->onBelowWriteBufferLowWatermark();
  EXPECT_EQ(0U, clusterCounter("upstream_flow_control_paused_reading_total"));
  EXPECT_EQ(0U, clusterCounter("upstream_flow_control_resumed_reading_total"));

  // Router commits response headers: sets downstreamResponseStarted() and flushes deferred count.
  EXPECT_CALL(*mock_upstream_, readDisable(true));
  EXPECT_CALL(upstream_filter_ds_cb, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, false))
      .WillOnce([&](uint64_t, Http::ResponseHeaderMapPtr&&, UpstreamRequest& req, bool) {
        ON_CALL(router_filter_interface_, downstreamResponseStarted()).WillByDefault(Return(true));
        req.maybeHandleDeferredReadDisable();
      });
  auto response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  bridge_->decodeHeaders(std::move(response_headers), false);
  EXPECT_EQ(1U, clusterCounter("upstream_flow_control_paused_reading_total"));

  EXPECT_CALL(*mock_upstream_, readDisable(false));
  EXPECT_CALL(upstream_filter_ds_cb, onBelowWriteBufferLowWatermark());
  router_ds_wm_cb_->onBelowWriteBufferLowWatermark();
  EXPECT_EQ(1U, clusterCounter("upstream_flow_control_resumed_reading_total"));

  upstream_request_.reset();
}

// Retriable 503 headers do not flush deferred downstream watermarks, call upstream->readDisable(),
// or charge upstream_flow_control_paused_reading_total on the discarded attempt.
TEST_F(UpstreamRequestWatermarkTest, Retriable503DoesNotFlushDeferredWatermark) {
  initializeWithWatermarkFilter();
  startAndEstablishPool();
  ASSERT_NE(router_ds_wm_cb_, nullptr);

  testing::StrictMock<Http::MockDownstreamWatermarkCallbacks> upstream_filter_ds_cb;
  filter_->subscribeDownstreamWatermark(upstream_filter_ds_cb);

  ON_CALL(router_filter_interface_, downstreamResponseStarted()).WillByDefault(Return(false));
  router_ds_wm_cb_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(*mock_upstream_, readDisable(_)).Times(0);
  EXPECT_CALL(upstream_filter_ds_cb, onAboveWriteBufferHighWatermark()).Times(0);
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(503, _, _, false))
      .WillOnce([&](uint64_t, Http::ResponseHeaderMapPtr&&, UpstreamRequest& req, bool) {
        req.resetStream();
      });
  auto response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "503"}}));
  bridge_->decodeHeaders(std::move(response_headers), false);
  EXPECT_EQ(0U, clusterCounter("upstream_flow_control_paused_reading_total"));

  upstream_request_.reset();
}

// Downstream low watermarks arriving reentrantly during maybeHandleDeferredReadDisable() cancel
// remaining deferred counts or unwind cleanly without asserting.
TEST_F(UpstreamRequestWatermarkTest, DownstreamLowWatermarkDuringDeferredReadDisableDoesNotAssert) {
  initializeWithWatermarkFilter();
  startAndEstablishPool();
  ASSERT_NE(router_ds_wm_cb_, nullptr);

  ON_CALL(router_filter_interface_, downstreamResponseStarted()).WillByDefault(Return(false));
  router_ds_wm_cb_->onAboveWriteBufferHighWatermark();
  router_ds_wm_cb_->onAboveWriteBufferHighWatermark();

  int read_disable_depth = 0;
  ON_CALL(*mock_upstream_, readDisable(_)).WillByDefault([&](bool disable) {
    read_disable_depth += disable ? 1 : -1;
  });

  testing::NiceMock<Http::MockDownstreamWatermarkCallbacks> upstream_filter_ds_cb;
  EXPECT_CALL(upstream_filter_ds_cb, onAboveWriteBufferHighWatermark()).WillOnce([&] {
    // First low cancels the 2nd un-flushed deferred count; second low unwinds the 1st.
    router_ds_wm_cb_->onBelowWriteBufferLowWatermark();
    router_ds_wm_cb_->onBelowWriteBufferLowWatermark();
  });
  filter_->subscribeDownstreamWatermark(upstream_filter_ds_cb);

  ON_CALL(router_filter_interface_, downstreamResponseStarted()).WillByDefault(Return(true));
  upstream_request_->maybeHandleDeferredReadDisable();
  EXPECT_EQ(0, read_disable_depth);

  upstream_request_.reset();
}

// Reentrant stream reset inside maybeHandleDeferredReadDisable() stops further deferred flushes
// without UAF or dangling iterator traversal.
TEST_F(UpstreamRequestWatermarkTest, ReentrantResetDuringDeferredReadDisableStopsFlushing) {
  initializeWithWatermarkFilter();
  startAndEstablishPool();
  ASSERT_NE(router_ds_wm_cb_, nullptr);

  ON_CALL(router_filter_interface_, downstreamResponseStarted()).WillByDefault(Return(false));
  router_ds_wm_cb_->onAboveWriteBufferHighWatermark();
  router_ds_wm_cb_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(router_filter_interface_.callbacks_, resetStream(_, _)).WillOnce([&] {
    upstream_request_->cleanUp();
  });
  testing::StrictMock<Http::MockDownstreamWatermarkCallbacks> resetting_cb;
  EXPECT_CALL(resetting_cb, onAboveWriteBufferHighWatermark()).WillOnce([&] {
    filter_->decoder_callbacks_->resetStream();
  });
  filter_->subscribeDownstreamWatermark(resetting_cb);

  // UpstreamCodecFilter was registered before resetting_cb, so it runs on the first deferred
  // callback before resetting_cb cleans up the stream; the second deferred callback must not run.
  EXPECT_CALL(*mock_upstream_, readDisable(true));
  ON_CALL(router_filter_interface_, downstreamResponseStarted()).WillByDefault(Return(true));
  upstream_request_->maybeHandleDeferredReadDisable();
  EXPECT_TRUE(upstream_request_->cleanedUp());

  upstream_request_.reset();
}

// Self-unsubscribing inside DownstreamWatermarkCallbacks or UpstreamWatermarkCallbacks must not
// invalidate iteration or cause heap-use-after-free.
TEST_F(UpstreamRequestWatermarkTest, ReentrantSelfUnsubscribeInWatermarkCallbacksDoesNotUaf) {
  initializeWithWatermarkFilter(/*add_second_filter=*/true);
  startAndEstablishPool();
  ASSERT_NE(router_ds_wm_cb_, nullptr);

  // 1. Downstream watermark self-unsubscribe still notifies UpstreamCodecFilter.
  testing::StrictMock<Http::MockDownstreamWatermarkCallbacks> self_removing_ds_cb;
  EXPECT_CALL(self_removing_ds_cb, onAboveWriteBufferHighWatermark()).WillOnce([&] {
    filter_->decoder_callbacks_->removeDownstreamWatermarkCallbacks(self_removing_ds_cb);
    filter_->downstream_wm_cb_ = nullptr;
  });
  filter_->subscribeDownstreamWatermark(self_removing_ds_cb);

  ON_CALL(router_filter_interface_, downstreamResponseStarted()).WillByDefault(Return(true));
  EXPECT_CALL(*mock_upstream_, readDisable(true));
  router_ds_wm_cb_->onAboveWriteBufferHighWatermark();

  // 2. Upstream watermark self-unsubscribe still notifies the sibling subscriber.
  testing::StrictMock<Http::MockUpstreamWatermarkCallbacks> self_removing_us_cb;
  testing::StrictMock<Http::MockUpstreamWatermarkCallbacks> sibling_us_cb;
  EXPECT_CALL(self_removing_us_cb, onAboveWriteBufferHighWatermark()).WillOnce([&] {
    filter_->decoder_callbacks_->removeUpstreamWatermarkCallbacks(self_removing_us_cb);
    filter_->upstream_wm_cb_ = nullptr;
  });
  EXPECT_CALL(sibling_us_cb, onAboveWriteBufferHighWatermark());
  filter_->subscribeUpstreamWatermark(self_removing_us_cb);
  second_filter_->subscribeUpstreamWatermark(sibling_us_cb);

  bridge_->onAboveWriteBufferHighWatermark();

  upstream_request_.reset();
}

// Upstream encoder filter backpressure (onEncoderFilterAboveWriteBufferHighWatermark) immediately
// pauses upstream reading even before downstreamResponseStarted() and composes cleanly with
// deferred/active downstream backpressure.
TEST_F(UpstreamRequestWatermarkTest,
       UpstreamEncoderFilterWatermarkComposesWithDownstreamWatermark) {
  initializeWithWatermarkFilter();
  startAndEstablishPool();
  ASSERT_NE(router_ds_wm_cb_, nullptr);

  testing::StrictMock<Http::MockDownstreamWatermarkCallbacks> upstream_filter_ds_cb;
  filter_->subscribeDownstreamWatermark(upstream_filter_ds_cb);

  ON_CALL(router_filter_interface_, downstreamResponseStarted()).WillByDefault(Return(false));
  EXPECT_CALL(*mock_upstream_, readDisable(true));
  EXPECT_CALL(upstream_filter_ds_cb, onAboveWriteBufferHighWatermark());
  filter_->encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark();
  EXPECT_EQ(1U, clusterCounter("upstream_flow_control_paused_reading_total"));

  EXPECT_CALL(*mock_upstream_, readDisable(_)).Times(0);
  router_ds_wm_cb_->onAboveWriteBufferHighWatermark();

  EXPECT_CALL(*mock_upstream_, readDisable(true));
  EXPECT_CALL(upstream_filter_ds_cb, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(router_filter_interface_, onUpstreamHeaders(_, _, _, false))
      .WillOnce([&](uint64_t, Http::ResponseHeaderMapPtr&&, UpstreamRequest& req, bool) {
        ON_CALL(router_filter_interface_, downstreamResponseStarted()).WillByDefault(Return(true));
        req.maybeHandleDeferredReadDisable();
      });
  auto response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  bridge_->decodeHeaders(std::move(response_headers), false);
  EXPECT_EQ(2U, clusterCounter("upstream_flow_control_paused_reading_total"));

  EXPECT_CALL(*mock_upstream_, readDisable(false));
  EXPECT_CALL(upstream_filter_ds_cb, onBelowWriteBufferLowWatermark());
  filter_->encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark();

  EXPECT_CALL(*mock_upstream_, readDisable(false));
  EXPECT_CALL(upstream_filter_ds_cb, onBelowWriteBufferLowWatermark());
  router_ds_wm_cb_->onBelowWriteBufferLowWatermark();
  EXPECT_EQ(2U, clusterCounter("upstream_flow_control_resumed_reading_total"));

  upstream_request_.reset();
}

// Encoder watermarks fired before onPoolReady() or after clearRequestEncoder() must not
// null-dereference upstream() or issue unbalanced readDisable(false) calls.
TEST_F(UpstreamRequestWatermarkTest, PrePoolReadyAndPostClearEncoderWatermarksAreSafe) {
  initializeWithWatermarkFilter();

  filter_->encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark();
  EXPECT_EQ(0U, clusterCounter("upstream_flow_control_paused_reading_total"));

  startAndEstablishPool();

  EXPECT_CALL(*mock_upstream_, readDisable(_)).Times(0);
  filter_->encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark();
  EXPECT_EQ(0U, clusterCounter("upstream_flow_control_resumed_reading_total"));

  EXPECT_CALL(*mock_upstream_, readDisable(true));
  filter_->encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark();
  upstream_request_->clearRequestEncoder();
  filter_->encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark();
  EXPECT_EQ(0U, clusterCounter("upstream_flow_control_resumed_reading_total"));

  upstream_request_.reset();
}

// Lowering an encoder watermark raised before onPoolReady() must not steal an active downstream
// readDisable(true) and prematurely resume upstream reading.
TEST_F(UpstreamRequestWatermarkTest, PrePoolHighWatermarkNotStolenByDownstreamLowWatermark) {
  initializeWithWatermarkFilter();

  // 1. Pre-pool encoder high watermark is recorded as unapplied.
  filter_->encoder_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark();

  startAndEstablishPool();
  ASSERT_NE(router_ds_wm_cb_, nullptr);

  int read_disable_depth = 0;
  ON_CALL(*mock_upstream_, readDisable(_)).WillByDefault([&](bool disable) {
    read_disable_depth += disable ? 1 : -1;
  });

  // 2. Response starts and downstream backs up -> upstream readDisable(true) applied.
  ON_CALL(router_filter_interface_, downstreamResponseStarted()).WillByDefault(Return(true));
  auto response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "200"}}));
  bridge_->decodeHeaders(std::move(response_headers), false);
  router_ds_wm_cb_->onAboveWriteBufferHighWatermark();
  ASSERT_EQ(1, read_disable_depth);

  // 3. Lowering the pre-pool watermark absorbs the unapplied high watermark without resuming.
  filter_->encoder_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark();
  EXPECT_EQ(1, read_disable_depth);

  // 4. Downstream drains -> upstream resumes cleanly.
  router_ds_wm_cb_->onBelowWriteBufferLowWatermark();
  EXPECT_EQ(0, read_disable_depth);

  upstream_request_.reset();
}

} // namespace
} // namespace Router
} // namespace Envoy
