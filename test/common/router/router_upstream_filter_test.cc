#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "source/common/router/router.h"
#include "source/common/router/upstream_codec_filter.h"

#include "test/common/http/common.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
namespace Envoy {
namespace Router {
namespace {

using envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter;

class TestFilter : public Filter {
public:
  using Filter::Filter;

  // Filter
  RetryStatePtr createRetryState(const RetryPolicy&, Http::RequestHeaderMap&,
                                 const Upstream::ClusterInfo&, const VirtualCluster*,
                                 RouteStatsContextOptRef, Runtime::Loader&,
                                 Random::RandomGenerator&, Event::Dispatcher&, TimeSource&,
                                 Upstream::ResourcePriority) override {
    EXPECT_EQ(nullptr, retry_state_);
    retry_state_ = new NiceMock<MockRetryState>();
    return RetryStatePtr{retry_state_};
  }

  const Network::Connection* downstreamConnection() const override {
    return &downstream_connection_;
  }

  NiceMock<Network::MockConnection> downstream_connection_;
  MockRetryState* retry_state_{};
};

class RouterUpstreamFilterTest : public testing::Test {
public:
  void init(std::vector<HttpFilter> upstream_filters) {
    envoy::extensions::filters::http::router::v3::Router router_proto;
    static const std::string cluster_name = "cluster_0";

    cluster_info_ = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
    ON_CALL(*cluster_info_, name()).WillByDefault(ReturnRef(cluster_name));
    ON_CALL(*cluster_info_, observabilityName()).WillByDefault(ReturnRef(cluster_name));
    ON_CALL(callbacks_.stream_info_, upstreamClusterInfo()).WillByDefault(Return(cluster_info_));
    EXPECT_CALL(callbacks_.dispatcher_, deferredDelete_).Times(testing::AnyNumber());
    for (const auto& filter : upstream_filters) {
      *router_proto.add_upstream_http_filters() = filter;
    }

    Stats::StatNameManagedStorage prefix("prefix", context_.scope().symbolTable());
    config_ = std::make_shared<FilterConfig>(prefix.statName(), context_,
                                             ShadowWriterPtr(new MockShadowWriter()), router_proto);
    router_ = std::make_shared<TestFilter>(*config_, config_->default_stats_);
    router_->setDecoderFilterCallbacks(callbacks_);
    EXPECT_CALL(callbacks_.dispatcher_, pushTrackedObject(_)).Times(testing::AnyNumber());
    EXPECT_CALL(callbacks_.dispatcher_, popTrackedObject(_)).Times(testing::AnyNumber());

    upstream_locality_.set_zone("to_az");
    context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters(
        {"fake_cluster"});
    ON_CALL(
        *context_.server_factory_context_.cluster_manager_.thread_local_cluster_.conn_pool_.host_,
        address())
        .WillByDefault(Return(host_address_));
    ON_CALL(
        *context_.server_factory_context_.cluster_manager_.thread_local_cluster_.conn_pool_.host_,
        locality())
        .WillByDefault(ReturnRef(upstream_locality_));
    router_->downstream_connection_.stream_info_.downstream_connection_info_provider_
        ->setLocalAddress(host_address_);
    router_->downstream_connection_.stream_info_.downstream_connection_info_provider_
        ->setRemoteAddress(Network::Utility::parseInternetAddressAndPort("1.2.3.4:80"));
  }

  Http::TestRequestHeaderMapImpl run() {
    NiceMock<Http::MockRequestEncoder> encoder;
    Http::ResponseDecoder* response_decoder = nullptr;

    EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.conn_pool_,
                newStream(_, _, _))
        .WillOnce(
            Invoke([&](Http::ResponseDecoder& decoder, Http::ConnectionPool::Callbacks& callbacks,
                       const Http::ConnectionPool::Instance::StreamOptions&)
                       -> Http::ConnectionPool::Cancellable* {
              response_decoder = &decoder;
              EXPECT_CALL(encoder.stream_, connectionInfoProvider())
                  .WillRepeatedly(ReturnRef(connection_info1_));
              callbacks.onPoolReady(encoder,
                                    context_.server_factory_context_.cluster_manager_
                                        .thread_local_cluster_.conn_pool_.host_,
                                    stream_info_, Http::Protocol::Http10);
              return nullptr;
            }));

    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    router_->decodeHeaders(headers, true);

    EXPECT_CALL(*router_->retry_state_, shouldRetryHeaders(_, _, _))
        .WillOnce(Return(RetryStatus::No));

    Http::ResponseHeaderMapPtr response_headers(new Http::TestResponseHeaderMapImpl());
    response_headers->setStatus(200);

    EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.conn_pool_
                    .host_->outlier_detector_,
                putHttpResponseCode(200));
    // NOLINTNEXTLINE(clang-analyzer-core.CallAndMessage)
    response_decoder->decodeHeaders(std::move(response_headers), true);
    return headers;
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;

  envoy::config::core::v3::Locality upstream_locality_;
  Network::Address::InstanceConstSharedPtr host_address_{
      Network::Utility::resolveUrl("tcp://10.0.0.5:9211")};
  Network::Address::InstanceConstSharedPtr upstream_local_address1_{
      Network::Utility::resolveUrl("tcp://10.0.0.5:10211")};
  Network::ConnectionInfoSetterImpl connection_info1_{upstream_local_address1_,
                                                      upstream_local_address1_};

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<TestFilter> router_;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(RouterUpstreamFilterTest, UpstreamFilter) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.no_extension_lookup_by_name", "false"}});

  HttpFilter add_header_filter;
  add_header_filter.set_name("add-header-filter");
  HttpFilter codec_filter;
  codec_filter.set_name("envoy.filters.http.upstream_codec");
  init({add_header_filter, codec_filter});
  auto headers = run();
  EXPECT_FALSE(headers.get(Http::LowerCaseString("x-header-to-add")).empty());
}
} // namespace
} // namespace Router
} // namespace Envoy
