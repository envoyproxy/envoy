#include "common/buffer/buffer_impl.h"
#include "common/config/filter_json.h"
#include "common/http/filter/ip_tagging_filter.h"
#include "common/http/header_map_impl.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

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
namespace Http {

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

  void SetUpTest(const std::string& yaml) {
    envoy::config::filter::http::ip_tagging::v2::IPTagging config;
    MessageUtil::loadFromYaml(yaml, config);
    config_.reset(new IpTaggingFilterConfig(config, "prefix.", stats_, runtime_));
    filter_.reset(new IpTaggingFilter(config_));
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  ~IpTaggingFilterTest() { filter_->onDestroy(); }

  IpTaggingFilterConfigSharedPtr config_;
  std::unique_ptr<IpTaggingFilter> filter_;
  NiceMock<MockStreamDecoderFilterCallbacks> filter_callbacks_;
  Buffer::OwnedImpl data_;
  NiceMock<Stats::MockStore> stats_;
  NiceMock<Runtime::MockLoader> runtime_;
};

TEST_F(IpTaggingFilterTest, JSONv1ConfigTest) {
  std::string internal_request_json = R"EOF(
    {
      "request_type" : "INTERNAL",
      "ip_tags" : [
        {
          "ip_tag_name" : "test_internal",
          "ip_list" : ["1.2.3.5/32"]
        }
      ]
    }
  )EOF";

  Envoy::Json::ObjectSharedPtr json_config =
      Envoy::Json::Factory::loadFromString(internal_request_json);
  envoy::config::filter::http::ip_tagging::v2::IPTagging proto_config;
  Config::FilterJson::translateIpTaggingFilterConfig(*json_config, proto_config);
  config_.reset(new IpTaggingFilterConfig(proto_config, "prefix.", stats_, runtime_));
  filter_.reset(new IpTaggingFilter(config_));
  filter_->setDecoderFilterCallbacks(filter_callbacks_);

  TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};
  EXPECT_EQ(FilterRequestType::INTERNAL, config_->requestType());

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("1.2.3.5");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.test_internal.hit")).Times(1);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total")).Times(1);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("test_internal", request_headers.get_(Headers::get().EnvoyIpTags));

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));
}

TEST_F(IpTaggingFilterTest, InternalRequest) {
  SetUpTest(internal_request_yaml);
  EXPECT_EQ(FilterRequestType::INTERNAL, config_->requestType());
  TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("1.2.3.5");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));

  EXPECT_CALL(stats_, counter("prefix.ip_tagging.internal_request.hit")).Times(1);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total")).Times(1);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("internal_request", request_headers.get_(Headers::get().EnvoyIpTags));

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));
}

TEST_F(IpTaggingFilterTest, ExternalRequest) {
  std::string external_request_yaml = R"EOF(
request_type: external
ip_tags:
  - ip_tag_name: external_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
)EOF";
  SetUpTest(external_request_yaml);
  EXPECT_EQ(FilterRequestType::EXTERNAL, config_->requestType());
  TestHeaderMapImpl request_headers;

  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total")).Times(1);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.external_request.hit")).Times(1);

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("1.2.3.4");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("external_request", request_headers.get_(Headers::get().EnvoyIpTags));

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));
}

TEST_F(IpTaggingFilterTest, BothRequest) {
  std::string both_request_yaml = R"EOF(
request_type: both
ip_tags:
  - ip_tag_name: external_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
  - ip_tag_name: internal_request
    ip_list:
      - {address_prefix: 1.2.3.5, prefix_len: 32}
)EOF";

  SetUpTest(both_request_yaml);
  EXPECT_EQ(FilterRequestType::BOTH, config_->requestType());
  TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total")).Times(2);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.internal_request.hit")).Times(1);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.external_request.hit")).Times(1);

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("1.2.3.5");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("internal_request", request_headers.get_(Headers::get().EnvoyIpTags));

  request_headers = {};
  remote_address = Network::Utility::parseInternetAddress("1.2.3.4");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("external_request", request_headers.get_(Headers::get().EnvoyIpTags));
}

TEST_F(IpTaggingFilterTest, NoHits) {
  SetUpTest(internal_request_yaml);
  TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("10.2.3.5");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.no_hit")).Times(1);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total")).Times(1);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));
}

TEST_F(IpTaggingFilterTest, AppendEntry) {
  SetUpTest(internal_request_yaml);
  TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}, {"x-envoy-ip-tags", "test"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("1.2.3.5");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("test, internal_request", request_headers.get_(Headers::get().EnvoyIpTags));

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));
}

TEST_F(IpTaggingFilterTest, NestedPrefixes) {
  std::string duplicate_request_yaml = R"EOF(
request_type: both
ip_tags:
  - ip_tag_name: duplicate_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
  - ip_tag_name: internal_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
)EOF";

  SetUpTest(duplicate_request_yaml);

  TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}, {"x-envoy-ip-tags", "test"}};

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("1.2.3.4");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));

  EXPECT_CALL(stats_, counter("prefix.ip_tagging.total")).Times(1);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.internal_request.hit")).Times(1);
  EXPECT_CALL(stats_, counter("prefix.ip_tagging.duplicate_request.hit")).Times(1);

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));

  // There is no guarantee for the order tags are returned by the LC-Trie.
  std::string header_tag_data = request_headers.get_(Headers::get().EnvoyIpTags.get());
  EXPECT_NE(std::string::npos, header_tag_data.find("test"));
  EXPECT_NE(std::string::npos, header_tag_data.find("internal_request"));
  EXPECT_NE(std::string::npos, header_tag_data.find("duplicate_request"));

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));
}

TEST_F(IpTaggingFilterTest, Ipv6Address) {
  const std::string ipv6_addresses_yaml = R"EOF(
ip_tags:
  - ip_tag_name: ipv6_request
    ip_list:
      - {address_prefix: 2001:abcd:ef01:2345:6789:abcd:ef01:234, prefix_len: 64}
)EOF";
  SetUpTest(ipv6_addresses_yaml);
  TestHeaderMapImpl request_headers;

  Network::Address::InstanceConstSharedPtr remote_address =
      Network::Utility::parseInternetAddress("2001:abcd:ef01:2345::1");
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress())
      .WillOnce(ReturnRef(remote_address));

  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ("ipv6_request", request_headers.get_(Headers::get().EnvoyIpTags));

  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));
}

TEST_F(IpTaggingFilterTest, RuntimeDisabled) {
  SetUpTest(internal_request_yaml);
  TestHeaderMapImpl request_headers{{"x-envoy-internal", "true"}};

  EXPECT_CALL(runtime_.snapshot_, featureEnabled("ip_tagging.http_filter_enabled", 100))
      .WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.request_info_, downstreamRemoteAddress()).Times(0);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_FALSE(request_headers.has(Headers::get().EnvoyIpTags));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(FilterTrailersStatus::Continue, filter_->decodeTrailers(request_headers));
}

} // namespace Http
} // namespace Envoy
