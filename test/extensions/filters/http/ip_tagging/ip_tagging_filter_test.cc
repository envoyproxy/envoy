#include "common/buffer/buffer_impl.h"
#include "common/config/filter_json.h"
#include "common/http/header_map_impl.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "extensions/filters/http/ip_tagging/ip_tagging_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace IpTagging {

class IpTaggingFilterTest : public testing::Test {
public:
  IpTaggingFilterTest() {
    ON_CALL(runtime_.snapshot_, featureEnabled("ip_tagging.http_filter_enabled", 100))
        .WillByDefault(Return(true));
  }

  const std::string internal_request_yaml = R"EOF(
request_type: internal
ip_tags:
  - ip_tag_name: internal_request
    ip_list:
      - {address_prefix: 1.2.3.5, prefix_len: 32}

)EOF";

  void initializeFilter(const std::string& yaml) {
    envoy::config::filter::http::ip_tagging::v2::IPTagging config;
    MessageUtil::loadFromYaml(yaml, config);
    config_.reset(new IpTaggingFilterConfig(config, "prefix.", stats_, runtime_));
    filter_.reset(new IpTaggingFilter(config_));
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  ~IpTaggingFilterTest() { filter_->onDestroy(); }

  IpTaggingFilterConfigSharedPtr config_;
  std::unique_ptr<IpTaggingFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> filter_callbacks_;
  Buffer::OwnedImpl data_;
  NiceMock<Stats::MockStore> stats_;
  NiceMock<Runtime::MockLoader> runtime_;
};

TEST_F(IpTaggingFilterTest, InternalRequest) {
  initializeFilter(internal_request_yaml);
  EXPECT_EQ(FilterRequestType::INTERNAL, config_->requestType());
  Http::TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("1.2.3.5");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));

  EXPECT_CALL(stats_, counter("prefix.ip_tagging.internal_request.hit")).Times(1);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total")).Times(1);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("internal_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));

  // Check external requests don't get a tag.
  request_headers = {};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_FALSE(request_headers.has(Http::Headers::get().EnvoyIpTags));
}

TEST_F(IpTaggingFilterTest, ExternalRequest) {
  const std::string external_request_yaml = R"EOF(
request_type: external
ip_tags:
  - ip_tag_name: external_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
)EOF";
  initializeFilter(external_request_yaml);
  EXPECT_EQ(FilterRequestType::EXTERNAL, config_->requestType());
  Http::TestHeaderMapImpl request_headers;

  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total")).Times(1);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.external_request.hit")).Times(1);

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("1.2.3.4");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("external_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));

  // Check internal requests don't get a tag.
  request_headers = {{"x-envoy-internal", "true"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_FALSE(request_headers.has(Http::Headers::get().EnvoyIpTags));
}

TEST_F(IpTaggingFilterTest, BothRequest) {
  const std::string both_request_yaml = R"EOF(
request_type: both
ip_tags:
  - ip_tag_name: external_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
  - ip_tag_name: internal_request
    ip_list:
      - {address_prefix: 1.2.3.5, prefix_len: 32}
)EOF";

  initializeFilter(both_request_yaml);
  EXPECT_EQ(FilterRequestType::BOTH, config_->requestType());
  Http::TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total")).Times(2);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.internal_request.hit")).Times(1);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.external_request.hit")).Times(1);

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("1.2.3.5");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("internal_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  request_headers = {};
  remote_address = Network::Utility::parseInternetAddress("1.2.3.4");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("external_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));
}

TEST_F(IpTaggingFilterTest, NoHits) {
  initializeFilter(internal_request_yaml);
  Http::TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("10.2.3.5");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.no_hit")).Times(1);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total")).Times(1);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_FALSE(request_headers.has(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));
}

TEST_F(IpTaggingFilterTest, AppendEntry) {
  initializeFilter(internal_request_yaml);
  Http::TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"},
                                          {"x-envoy-ip-tags", "test"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("1.2.3.5");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("test,internal_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));
}

TEST_F(IpTaggingFilterTest, NestedPrefixes) {
  const std::string duplicate_request_yaml = R"EOF(
request_type: both
ip_tags:
  - ip_tag_name: duplicate_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
  - ip_tag_name: internal_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
)EOF";

  initializeFilter(duplicate_request_yaml);
  Http::TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"},
                                          {"x-envoy-ip-tags", "test"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("1.2.3.4");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));

  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total")).Times(1);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.internal_request.hit")).Times(1);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.duplicate_request.hit")).Times(1);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  // There is no guarantee for the order tags are returned by the LC-Trie.
  const std::string header_tag_data = request_headers.get_(Http::Headers::get().EnvoyIpTags.get());
  EXPECT_NE(std::string::npos, header_tag_data.find("test"));
  EXPECT_NE(std::string::npos, header_tag_data.find("internal_request"));
  EXPECT_NE(std::string::npos, header_tag_data.find("duplicate_request"));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));
}

TEST_F(IpTaggingFilterTest, Ipv6Address) {
  const std::string ipv6_addresses_yaml = R"EOF(
ip_tags:
  - ip_tag_name: ipv6_request
    ip_list:
      - {address_prefix: 2001:abcd:ef01:2345:6789:abcd:ef01:234, prefix_len: 64}
)EOF";
  initializeFilter(ipv6_addresses_yaml);
  Http::TestHeaderMapImpl request_headers;

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("2001:abcd:ef01:2345::1");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("ipv6_request", request_headers.get_(Http::Headers::get().EnvoyIpTags));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));
}

TEST_F(IpTaggingFilterTest, RuntimeDisabled) {
  initializeFilter(internal_request_yaml);
  Http::TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ip_tagging.http_filter_enabled", 100))
      .WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress()).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_FALSE(request_headers.has(Http::Headers::get().EnvoyIpTags));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));
}

} // namespace IpTagging
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
