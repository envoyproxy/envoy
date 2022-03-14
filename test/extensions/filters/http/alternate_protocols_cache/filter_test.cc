#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"

#include "source/extensions/filters/http/alternate_protocols_cache/filter.h"

#include "test/mocks/http/alternate_protocols_cache.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/simulated_time_system.h"

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
            std::make_shared<Http::MockAlternateProtocolsCacheManager>()),
        alternate_protocols_cache_(std::make_shared<Http::MockAlternateProtocolsCache>()) {
    initialize(true);
  }

  void initialize(bool populate_config) {
    EXPECT_CALL(alternate_protocols_cache_manager_factory_, get())
        .WillOnce(Return(alternate_protocols_cache_manager_));

    envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig proto_config;
    if (populate_config) {
      proto_config.mutable_alternate_protocols_cache_options()->set_name("foo");
      EXPECT_CALL(*alternate_protocols_cache_manager_, getCache(_, _))
          .WillOnce(Return(alternate_protocols_cache_));
    }
    filter_config_ = std::make_shared<FilterConfig>(
        proto_config, alternate_protocols_cache_manager_factory_, simTime());
    filter_ = std::make_unique<Filter>(filter_config_, dispatcher_);
    filter_->setEncoderFilterCallbacks(callbacks_);
  }

  Event::MockDispatcher dispatcher_;
  Http::MockAlternateProtocolsCacheManagerFactory alternate_protocols_cache_manager_factory_;
  std::shared_ptr<Http::MockAlternateProtocolsCacheManager> alternate_protocols_cache_manager_;
  std::shared_ptr<Http::MockAlternateProtocolsCache> alternate_protocols_cache_;
  FilterConfigSharedPtr filter_config_;
  std::unique_ptr<Filter> filter_;
  Http::MockStreamEncoderFilterCallbacks callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_{{":authority", "foo"}};
};

std::string dumpOrigin(const Http::AlternateProtocolsCache::Origin& origin) {
  return "{ scheme: '" + origin.scheme_ + "' host: '" + origin.hostname_ +
         "' port: " + absl::StrCat(origin.port_) + " }\n";
}

std::string dumpAlternative(const Http::AlternateProtocolsCache::AlternateProtocol& origin) {
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
  Http::AlternateProtocolsCache::Origin expected_origin("https", "host1", 443);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  filter_->onDestroy();
}

TEST_F(FilterTest, InvalidAltSvc) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"alt-svc", "garbage"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  filter_->onDestroy();
}

TEST_F(FilterTest, ValidAltSvc) {
  Http::TestResponseHeaderMapImpl headers{
      {":status", "200"}, {"alt-svc", "h3-29=\":443\"; ma=86400, h3=\":443\"; ma=60"}};
  Http::AlternateProtocolsCache::Origin expected_origin("https", "host1", 443);
  MonotonicTime now = simTime().monotonicTime();
  const std::vector<Http::AlternateProtocolsCache::AlternateProtocol> expected_protocols = {
      Http::AlternateProtocolsCache::AlternateProtocol("h3-29", "", 443,
                                                       now + std::chrono::seconds(86400)),
      Http::AlternateProtocolsCache::AlternateProtocol("h3", "", 443,
                                                       now + std::chrono::seconds(60)),
  };

  std::shared_ptr<Network::MockResolvedAddress> address =
      std::make_shared<Network::MockResolvedAddress>("1.2.3.4:443", "1.2.3.4:443");
  Network::MockIp ip;
  std::string hostname = "host1";
  std::shared_ptr<Upstream::MockHostDescription> hd =
      std::make_shared<Upstream::MockHostDescription>();
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  EXPECT_CALL(callbacks_, streamInfo()).WillOnce(ReturnRef(stream_info));
  stream_info.upstreamInfo()->setUpstreamHost(hd);
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

} // namespace
} // namespace AlternateProtocolsCache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
