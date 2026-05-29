#include "source/common/network/application_protocol.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "contrib/istio/filters/http/alpn/source/alpn_filter.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig;
using istio::envoy::config::filter::http::alpn::v2alpha1::FilterConfig_AlpnOverride;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Http {
namespace Alpn {
namespace {

class AlpnFilterTest : public testing::Test {
public:
  std::unique_ptr<AlpnFilter> makeAlpnOverrideFilter(const AlpnOverrides& alpn) {
    FilterConfig proto_config;

    for (const auto& p : alpn) {
      FilterConfig_AlpnOverride entry;
      entry.set_upstream_protocol(getProtocol(p.first));
      for (const auto& v : p.second) {
        entry.add_alpn_override(v);
      }
      proto_config.mutable_alpn_override()->Add(std::move(entry));
    }

    auto config = std::make_shared<AlpnFilterConfig>(proto_config, cluster_manager_);
    auto filter = std::make_unique<AlpnFilter>(config);
    filter->setDecoderFilterCallbacks(callbacks_);
    return filter;
  }

protected:
  FilterConfig::Protocol getProtocol(Http::Protocol protocol) {
    switch (protocol) {
    case Http::Protocol::Http10:
      return FilterConfig::Protocol::FilterConfig_Protocol_HTTP10;
    case Http::Protocol::Http11:
      return FilterConfig::Protocol::FilterConfig_Protocol_HTTP11;
    case Http::Protocol::Http2:
      return FilterConfig::Protocol::FilterConfig_Protocol_HTTP2;
    default:
      PANIC("not implemented");
    }
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  std::shared_ptr<Upstream::MockThreadLocalCluster> fake_cluster_{
      std::make_shared<NiceMock<Upstream::MockThreadLocalCluster>>()};
  std::shared_ptr<Upstream::MockClusterInfo> cluster_info_{
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>()};
  Http::TestRequestHeaderMapImpl headers_;
};

TEST_F(AlpnFilterTest, OverrideAlpnUseDownstreamProtocol) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info));
  const AlpnOverrides alpn = {{Http::Protocol::Http10, {"foo", "bar"}},
                              {Http::Protocol::Http11, {"baz"}},
                              {Http::Protocol::Http2, {"qux"}}};
  auto filter = makeAlpnOverrideFilter(alpn);

  ON_CALL(cluster_manager_, getThreadLocalCluster(_)).WillByDefault(Return(fake_cluster_.get()));
  ON_CALL(*fake_cluster_, info()).WillByDefault(Return(cluster_info_));
  ON_CALL(*cluster_info_, upstreamHttpProtocol(_))
      .WillByDefault([](absl::optional<Http::Protocol> protocol) -> std::vector<Http::Protocol> {
        return {protocol.value()};
      });

  auto protocols = {Http::Protocol::Http10, Http::Protocol::Http11, Http::Protocol::Http2};
  for (const auto p : protocols) {
    EXPECT_CALL(stream_info, protocol()).WillOnce(Return(p));
    Envoy::StreamInfo::FilterStateSharedPtr filter_state(
        std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
            Envoy::StreamInfo::FilterState::LifeSpan::FilterChain));
    EXPECT_CALL(stream_info, filterState()).WillOnce(ReturnRef(filter_state));
    EXPECT_EQ(filter->decodeHeaders(headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_TRUE(
        filter_state->hasData<Network::ApplicationProtocols>(Network::ApplicationProtocols::key()));
    auto alpn_override =
        filter_state
            ->getDataReadOnly<Network::ApplicationProtocols>(Network::ApplicationProtocols::key())
            ->value();

    EXPECT_EQ(alpn_override, alpn.at(p));
  }
}

TEST_F(AlpnFilterTest, OverrideAlpn) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info));
  const AlpnOverrides alpn = {{Http::Protocol::Http10, {"foo", "bar"}},
                              {Http::Protocol::Http11, {"baz"}},
                              {Http::Protocol::Http2, {"qux"}}};
  auto filter = makeAlpnOverrideFilter(alpn);

  ON_CALL(cluster_manager_, getThreadLocalCluster(_)).WillByDefault(Return(fake_cluster_.get()));
  ON_CALL(*fake_cluster_, info()).WillByDefault(Return(cluster_info_));
  ON_CALL(*cluster_info_, upstreamHttpProtocol(_))
      .WillByDefault([](absl::optional<Http::Protocol>) -> std::vector<Http::Protocol> {
        return {Http::Protocol::Http2};
      });

  auto protocols = {Http::Protocol::Http10, Http::Protocol::Http11, Http::Protocol::Http2};
  for (const auto p : protocols) {
    EXPECT_CALL(stream_info, protocol()).WillOnce(Return(p));
    Envoy::StreamInfo::FilterStateSharedPtr filter_state(
        std::make_shared<Envoy::StreamInfo::FilterStateImpl>(
            Envoy::StreamInfo::FilterState::LifeSpan::FilterChain));
    EXPECT_CALL(stream_info, filterState()).WillOnce(ReturnRef(filter_state));
    EXPECT_EQ(filter->decodeHeaders(headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_TRUE(
        filter_state->hasData<Network::ApplicationProtocols>(Network::ApplicationProtocols::key()));
    auto alpn_override =
        filter_state
            ->getDataReadOnly<Network::ApplicationProtocols>(Network::ApplicationProtocols::key())
            ->value();

    EXPECT_EQ(alpn_override, alpn.at(Http::Protocol::Http2));
  }
}

TEST_F(AlpnFilterTest, EmptyOverrideAlpn) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info));
  const AlpnOverrides alpn = {{Http::Protocol::Http10, {"foo", "bar"}},
                              {Http::Protocol::Http11, {"baz"}}};
  auto filter = makeAlpnOverrideFilter(alpn);

  ON_CALL(cluster_manager_, getThreadLocalCluster(_)).WillByDefault(Return(fake_cluster_.get()));
  ON_CALL(*fake_cluster_, info()).WillByDefault(Return(cluster_info_));
  ON_CALL(*cluster_info_, upstreamHttpProtocol(_))
      .WillByDefault([](absl::optional<Http::Protocol>) -> std::vector<Http::Protocol> {
        return {Http::Protocol::Http2};
      });

  auto protocols = {Http::Protocol::Http10, Http::Protocol::Http11, Http::Protocol::Http2};
  for (const auto p : protocols) {
    EXPECT_CALL(stream_info, protocol()).WillOnce(Return(p));
    Envoy::StreamInfo::FilterStateImpl filter_state{Envoy::StreamInfo::FilterState::FilterChain};
    EXPECT_CALL(stream_info, filterState()).Times(0);
    EXPECT_EQ(filter->decodeHeaders(headers_, false), Http::FilterHeadersStatus::Continue);
    EXPECT_FALSE(
        filter_state.hasData<Network::ApplicationProtocols>(Network::ApplicationProtocols::key()));
  }
}

TEST_F(AlpnFilterTest, AlpnOverrideFalse) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  auto metadata = TestUtility::parseYaml<envoy::config::core::v3::Metadata>(R"EOF(
        filter_metadata:
          istio:
            alpn_override: "false"
      )EOF");

  ON_CALL(callbacks_, streamInfo()).WillByDefault(ReturnRef(stream_info));
  ON_CALL(cluster_manager_, getThreadLocalCluster(_)).WillByDefault(Return(fake_cluster_.get()));
  ON_CALL(*fake_cluster_, info()).WillByDefault(Return(cluster_info_));
  ON_CALL(*cluster_info_, metadata()).WillByDefault(ReturnRef(metadata));

  const AlpnOverrides alpn = {{Http::Protocol::Http10, {"foo", "bar"}},
                              {Http::Protocol::Http11, {"baz"}}};
  auto filter = makeAlpnOverrideFilter(alpn);

  EXPECT_CALL(*cluster_info_, upstreamHttpProtocol(_)).Times(0);
  EXPECT_EQ(filter->decodeHeaders(headers_, false), Http::FilterHeadersStatus::Continue);
}

} // namespace
} // namespace Alpn
} // namespace Http
} // namespace Envoy
