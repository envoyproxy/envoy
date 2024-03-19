#include "source/common/common/utility.h"
#include "source/common/network/utility.h"
#include "source/common/router/upstream_codec_filter.h"
#include "source/common/router/upstream_request.h"

#include "test/common/http/common.h"
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

class UpstreamRequestTest : public testing::Test {
public:
  UpstreamRequestTest() : pool_(*symbol_table_) {
    HttpTestUtility::addDefaultHeaders(downstream_request_header_map_);
    ON_CALL(router_filter_interface_, downstreamHeaders())
        .WillByDefault(Return(&downstream_request_header_map_));
  }

  void initialize() {
    auto conn_pool = std::make_unique<NiceMock<Router::MockGenericConnPool>>();
    conn_pool_ = conn_pool.get();
    upstream_request_ = std::make_unique<UpstreamRequest>(router_filter_interface_,
                                                          std::move(conn_pool), false, true);
  }
  Http::FilterFactoryCb createDecoderFilterFactoryCb(Http::StreamDecoderFilterSharedPtr filter) {
    return [filter](Http::FilterChainFactoryCallbacks& callbacks) {
      callbacks.addStreamDecoderFilter(filter);
    };
  }

  Router::MockGenericConnPool* conn_pool_{}; // Owned by the upstream request
  Http::TestRequestHeaderMapImpl downstream_request_header_map_{};
  Stats::TestUtil::TestSymbolTable symbol_table_;
  Stats::StatNamePool pool_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<MockRouterFilterInterface> router_filter_interface_;
  std::unique_ptr<Router::FilterConfig> router_config_; // must outlive `UpstreamRequest`
  std::unique_ptr<UpstreamRequest> upstream_request_;
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
  router_config_ = std::make_unique<Router::FilterConfig>(
      pool_.add("prefix"), context_, ShadowWriterPtr(new MockShadowWriter()), router_proto);
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
  router_config_ = std::make_unique<Router::FilterConfig>(
      pool_.add("prefix"), context_, ShadowWriterPtr(new MockShadowWriter()), router_proto);
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

  EXPECT_CALL(*router_filter_interface_.cluster_info_, createFilterChain)
      .Times(2)
      .WillRepeatedly(Invoke([&](Http::FilterChainManager& manager, bool only_create_if_configured,
                                 const Http::FilterChainOptions&) -> bool {
        if (only_create_if_configured) {
          return false;
        }
        auto factory = createDecoderFilterFactoryCb(filter);
        manager.applyFilterFactoryCb({}, factory);
        Http::FilterFactoryCb factory_cb =
            [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
          callbacks.addStreamDecoderFilter(std::make_shared<UpstreamCodecFilter>());
        };
        manager.applyFilterFactoryCb({}, factory_cb);
        return true;
      }));

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

  EXPECT_CALL(*conn_pool_, newStream(_))
      .WillOnce(Invoke([&](GenericConnectionPoolCallbacks* callbacks) {
        std::stringstream out;
        callbacks->upstreamToDownstream().dumpState(out, 0);
        std::string state = out.str();
        EXPECT_THAT(state, testing::HasSubstr("UpstreamRequest"));
        EXPECT_EQ(callbacks->upstreamToDownstream().connection().ptr(),
                  router_filter_interface_.callbacks_.connection().ptr());
        return nullptr;
      }));

  EXPECT_CALL(*filter, decodeHeaders(_, false));
  upstream_request_->acceptHeadersFromRouter(false);

  EXPECT_CALL(router_filter_interface_.callbacks_, resetStream(_, _));
  filter->callbacks_->resetStream();
}

TEST_F(UpstreamRequestTest, ConnectionPoolLatencyTime) {
  initialize();

  const auto latency_to_add = std::chrono::microseconds(10);

  EXPECT_CALL(*conn_pool_, newStream(_))
      .WillOnce(Invoke([&](GenericConnectionPoolCallbacks* callbacks) {
        router_filter_interface_.callbacks_.dispatcher_.globalTimeSystem().advanceTimeWait(
            latency_to_add);

        callbacks->onPoolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure,
                                 "Some Failure", nullptr);
        return nullptr;
      }));

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
      Network::Utility::parseInternetAddressAndPort("1.2.3.4:5678"));
  connection_info_provider->setLocalAddress(
      Network::Utility::parseInternetAddressAndPort("5.6.7.8:5678"));
  connection_info_provider->setDirectRemoteAddressForTest(
      Network::Utility::parseInternetAddressAndPort("1.2.3.4:5678"));

  // Dump State
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  Stats::TestUtil::MemoryTest memory_test;
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

} // namespace
} // namespace Router
} // namespace Envoy
