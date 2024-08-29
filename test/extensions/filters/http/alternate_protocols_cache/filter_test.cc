#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"

#include "source/extensions/filters/http/alternate_protocols_cache/filter.h"

#include "test/mocks/http/http_server_properties_cache.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AlternateProtocolsCache {
namespace {

class FilterTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  FilterTest()
      : alternate_protocols_cache_manager_(
            std::make_shared<Http::MockHttpServerPropertiesCacheManager>()),
        alternate_protocols_cache_(std::make_shared<Http::MockHttpServerPropertiesCache>()) {
    initialize(true);
  }

  void initialize(bool populate_config) {
    envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig proto_config;
    if (populate_config) {
      EXPECT_CALL(*alternate_protocols_cache_manager_, getCache(_, _))
          .Times(testing::AnyNumber())
          .WillOnce(Return(alternate_protocols_cache_));
    }
    filter_config_ = std::make_shared<FilterConfig>(proto_config,
                                                    *alternate_protocols_cache_manager_, simTime());
    filter_ = std::make_unique<Filter>(filter_config_, dispatcher_);
    filter_->setEncoderFilterCallbacks(callbacks_);
  }

  Event::MockDispatcher dispatcher_;
  std::shared_ptr<Http::MockHttpServerPropertiesCacheManager> alternate_protocols_cache_manager_;
  std::shared_ptr<Http::MockHttpServerPropertiesCache> alternate_protocols_cache_;
  FilterConfigSharedPtr filter_config_;
  std::unique_ptr<Filter> filter_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_{{":authority", "foo"}};
};

std::string dumpOrigin(const Http::HttpServerPropertiesCache::Origin& origin) {
  return "{ scheme: '" + origin.scheme_ + "' host: '" + origin.hostname_ +
         "' port: " + absl::StrCat(origin.port_) + " }\n";
}

std::string dumpAlternative(const Http::HttpServerPropertiesCache::AlternateProtocol& origin) {
  return "{ alpn: '" + origin.alpn_ + "' host: '" + origin.hostname_ +
         "' port: " + absl::StrCat(origin.port_) + " }\n";
}

TEST_F(FilterTest, NoCache) {
  initialize(false);

  Http::TestResponseHeaderMapImpl headers{
      {":status", "200"}, {"alt-svc", "h3-29=\":443\"; ma=86400, h3=\":443\"; ma=60"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  filter_->onDestroy();
}

TEST_F(FilterTest, NoAltSvc) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}};
  Http::HttpServerPropertiesCache::Origin expected_origin("https", "host1", 443);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  filter_->onDestroy();
}

TEST_F(FilterTest, InvalidAltSvc) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"alt-svc", "garbage"}};

  // Set up the cluster info correctly to have a cache configuration.
  envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig proto_config;
  auto info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  callbacks_.stream_info_.upstream_cluster_info_ = info;
  absl::optional<const envoy::config::core::v3::AlternateProtocolsCacheOptions> options =
      proto_config.alternate_protocols_cache_options();
  ON_CALL(*info, alternateProtocolsCacheOptions()).WillByDefault(ReturnRef(options));
  EXPECT_CALL(*alternate_protocols_cache_manager_, getCache(_, _))
      .Times(testing::AnyNumber())
      .WillOnce(Return(alternate_protocols_cache_));
  EXPECT_CALL(callbacks_, streamInfo())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(ReturnRef(callbacks_.stream_info_));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  filter_->onDestroy();
}

TEST_F(FilterTest, ValidAltSvc) {
  Http::TestResponseHeaderMapImpl headers{
      {":status", "200"}, {"alt-svc", "h3-29=\":443\"; ma=86400, h3=\":443\"; ma=60"}};
  Http::HttpServerPropertiesCache::Origin expected_origin("https", "host1", 443);
  MonotonicTime now = simTime().monotonicTime();
  const std::vector<Http::HttpServerPropertiesCache::AlternateProtocol> expected_protocols = {
      Http::HttpServerPropertiesCache::AlternateProtocol("h3-29", "", 443,
                                                         now + std::chrono::seconds(86400)),
      Http::HttpServerPropertiesCache::AlternateProtocol("h3", "", 443,
                                                         now + std::chrono::seconds(60)),
  };

  std::shared_ptr<Network::MockResolvedAddress> address =
      std::make_shared<Network::MockResolvedAddress>("1.2.3.4:443", "1.2.3.4:443");
  Network::MockIp ip;
  std::string hostname = "host1";

  // Set up the cluster info correctly to have a cache configuration.
  envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig proto_config;
  auto info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  callbacks_.stream_info_.upstream_cluster_info_ = info;
  absl::optional<const envoy::config::core::v3::AlternateProtocolsCacheOptions> options =
      proto_config.alternate_protocols_cache_options();
  ON_CALL(*info, alternateProtocolsCacheOptions()).WillByDefault(ReturnRef(options));
  EXPECT_CALL(*alternate_protocols_cache_manager_, getCache(_, _))
      .Times(testing::AnyNumber())
      .WillOnce(Return(alternate_protocols_cache_));

  EXPECT_CALL(callbacks_, streamInfo())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(ReturnRef(callbacks_.stream_info_));
  EXPECT_CALL(callbacks_.stream_info_, upstreamClusterInfo())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(Return(info));
  EXPECT_CALL(callbacks_.stream_info_, upstreamInfo()).Times(testing::AtLeast(1));
  // Get the pointer to MockHostDescription.
  std::shared_ptr<const Upstream::MockHostDescription> hd =
      std::dynamic_pointer_cast<const Upstream::MockHostDescription>(
          callbacks_.stream_info_.upstreamInfo()->upstreamHost());
  EXPECT_CALL(*hd, hostname()).WillOnce(ReturnRef(hostname));
  EXPECT_CALL(*hd, address()).WillOnce(Return(address));
  EXPECT_CALL(*address, ip()).WillOnce(Return(&ip));
  EXPECT_CALL(ip, port()).WillOnce(Return(443));
  EXPECT_CALL(*alternate_protocols_cache_, setAlternatives(_, _))
      .WillOnce(testing::DoAll(
          testing::WithArg<0>(Invoke([expected_origin](auto& actual_origin) {
            EXPECT_EQ(expected_origin, actual_origin)
                << dumpOrigin(expected_origin) << dumpOrigin(actual_origin);
          })),
          testing::WithArg<1>(Invoke([expected_protocols](auto& actual_protocols) {
            EXPECT_EQ(expected_protocols, actual_protocols) << dumpAlternative(actual_protocols[0]);
            ;
          }))));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  filter_->onDestroy();
}

TEST_F(FilterTest, ValidAltSvcMissingPort) {
  Http::TestResponseHeaderMapImpl headers{
      {":status", "200"}, {"alt-svc", "h3-29=\":443\"; ma=86400, h3=\":443\"; ma=60"}};
  Http::HttpServerPropertiesCache::Origin expected_origin("https", "host1", 443);
  MonotonicTime now = simTime().monotonicTime();
  const std::vector<Http::HttpServerPropertiesCache::AlternateProtocol> expected_protocols = {
      Http::HttpServerPropertiesCache::AlternateProtocol("h3-29", "", 443,
                                                         now + std::chrono::seconds(86400)),
      Http::HttpServerPropertiesCache::AlternateProtocol("h3", "", 443,
                                                         now + std::chrono::seconds(60)),
  };

  std::string hostname = "host1";

  // Set up the cluster info correctly to have a cache configuration.
  envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig proto_config;
  auto info = std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  callbacks_.stream_info_.upstream_cluster_info_ = info;
  absl::optional<const envoy::config::core::v3::AlternateProtocolsCacheOptions> options =
      proto_config.alternate_protocols_cache_options();
  ON_CALL(*info, alternateProtocolsCacheOptions()).WillByDefault(ReturnRef(options));
  EXPECT_CALL(*alternate_protocols_cache_manager_, getCache(_, _))
      .Times(testing::AnyNumber())
      .WillOnce(Return(alternate_protocols_cache_));

  EXPECT_CALL(callbacks_, streamInfo())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(ReturnRef(callbacks_.stream_info_));
  EXPECT_CALL(callbacks_.stream_info_, upstreamClusterInfo())
      .Times(testing::AtLeast(1))
      .WillRepeatedly(Return(info));
  EXPECT_CALL(callbacks_.stream_info_, upstreamInfo()).Times(testing::AtLeast(1));
  // Get the pointer to MockHostDescription.
  std::shared_ptr<const Upstream::MockHostDescription> hd =
      std::dynamic_pointer_cast<const Upstream::MockHostDescription>(
          callbacks_.stream_info_.upstreamInfo()->upstreamHost());
  EXPECT_CALL(*hd, hostname()).WillOnce(ReturnRef(hostname));
  // The address() call returns nullptr, so we won't get a port, but the filter should use the
  // default port.
  EXPECT_CALL(*hd, address()).WillOnce(Return(nullptr));
  EXPECT_CALL(*alternate_protocols_cache_, setAlternatives(_, _))
      .WillOnce(testing::DoAll(
          testing::WithArg<0>(Invoke([expected_origin](auto& actual_origin) {
            EXPECT_EQ(expected_origin, actual_origin)
                << dumpOrigin(expected_origin) << dumpOrigin(actual_origin);
          })),
          testing::WithArg<1>(Invoke([expected_protocols](auto& actual_protocols) {
            EXPECT_EQ(expected_protocols, actual_protocols) << dumpAlternative(actual_protocols[0]);
            ;
          }))));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  filter_->onDestroy();
}

} // namespace
} // namespace AlternateProtocolsCache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
