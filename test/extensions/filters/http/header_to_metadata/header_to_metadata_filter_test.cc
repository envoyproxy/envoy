#include "common/http/header_map_impl.h"

#include "extensions/filters/http/header_to_metadata/header_to_metadata_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/request_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

class HeaderToMetadataTest : public testing::Test {
public:
  HeaderToMetadataTest() {}

  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-version
    on_header_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
    on_header_missing:
      metadata_namespace: envoy.lb
      key: default
      value: 'true'
      type: STRING
)EOF";

  void initializeFilter(const std::string& yaml) {
    envoy::config::filter::http::header_to_metadata::v2::Config config;
    MessageUtil::loadFromYaml(yaml, config);
    config_.reset(new Config(config));
    filter_.reset(new HeaderToMetadataFilter(config_));
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  ConfigSharedPtr config_;
  std::shared_ptr<HeaderToMetadataFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Envoy::RequestInfo::MockRequestInfo> req_info_;
};

MATCHER_P(MapEq, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(rhs.size() > 0);
  for (auto const& entry : rhs) {
    EXPECT_EQ(obj.fields().at(entry.first).string_value(), entry.second);
  }
  return true;
}

MATCHER_P(MapEqNum, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(rhs.size() > 0);
  for (auto const& entry : rhs) {
    EXPECT_EQ(obj.fields().at(entry.first).number_value(), entry.second);
  }
  return true;
}

/**
 * Basic use-case.
 */
TEST_F(HeaderToMetadataTest, BasicRequestTest) {
  initializeFilter(request_config_yaml);
  Http::TestHeaderMapImpl incoming_headers{{"X-VERSION", "0xdeadbeef"}};
  std::map<std::string, std::string> expected = {{"version", "0xdeadbeef"}};

  EXPECT_CALL(decoder_callbacks_, requestInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(incoming_headers, false));
}

/**
 * X-version not set, the on missing value should be set.
 */
TEST_F(HeaderToMetadataTest, DefaultEndpointsTest) {
  initializeFilter(request_config_yaml);
  Http::TestHeaderMapImpl incoming_headers{{"X-FOO", "bar"}};
  std::map<std::string, std::string> expected = {{"default", "true"}};

  EXPECT_CALL(decoder_callbacks_, requestInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(incoming_headers, false));
}

/**
 * Test that private headers get removed.
 */
TEST_F(HeaderToMetadataTest, HeaderRemovedTest) {
  const std::string response_config_yaml = R"EOF(
response_rules:
  - header: x-authenticated
    on_header_present:
      key: auth
      type: STRING
    remove: true
)EOF";
  initializeFilter(response_config_yaml);
  Http::TestHeaderMapImpl incoming_headers{{"x-authenticated", "1"}};
  std::map<std::string, std::string> expected = {{"auth", "1"}};
  Http::TestHeaderMapImpl empty_headers;

  EXPECT_CALL(encoder_callbacks_, requestInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_,
              setDynamicMetadata("envoy.filters.http.header_to_metadata", MapEq(expected)));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(incoming_headers, false));
  EXPECT_EQ(empty_headers, incoming_headers);
}

/**
 * Test the value gets written as a number.
 */
TEST_F(HeaderToMetadataTest, NumberTypeTest) {
  const std::string response_config_yaml = R"EOF(
response_rules:
  - header: x-authenticated
    on_header_present:
      key: auth
      type: NUMBER
)EOF";
  initializeFilter(response_config_yaml);
  Http::TestHeaderMapImpl incoming_headers{{"x-authenticated", "1"}};
  std::map<std::string, int> expected = {{"auth", 1}};
  Http::TestHeaderMapImpl empty_headers;

  EXPECT_CALL(encoder_callbacks_, requestInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_,
              setDynamicMetadata("envoy.filters.http.header_to_metadata", MapEqNum(expected)));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(incoming_headers, false));
}

/**
 * Headers not present.
 */
TEST_F(HeaderToMetadataTest, HeaderNotPresent) {
  const std::string config = R"EOF(
request_rules:
  - header: x-version
    on_header_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
)EOF";
  initializeFilter(config);
  Http::TestHeaderMapImpl incoming_headers{};

  EXPECT_CALL(decoder_callbacks_, requestInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(incoming_headers, false));
}

/**
 * Two headers match.
 */
TEST_F(HeaderToMetadataTest, MultipleHeadersMatch) {
  const std::string python_yaml = R"EOF(
request_rules:
  - header: x-version
    on_header_present:
      key: version
      metadata_namespace: envoy.lb
      type: STRING
  - header: x-python-version
    on_header_present:
      key: python_version
      metadata_namespace: envoy.lb
      type: STRING
)EOF";
  initializeFilter(python_yaml);
  Http::TestHeaderMapImpl incoming_headers{
      {"X-VERSION", "v4.0"},
      {"X-PYTHON-VERSION", "3.7"},
      {"X-IGNORE", "nothing"},
  };
  std::map<std::string, std::string> expected = {{"version", "v4.0"}, {"python_version", "3.7"}};

  EXPECT_CALL(decoder_callbacks_, requestInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(incoming_headers, false));
}

/**
 * No header value.
 */
TEST_F(HeaderToMetadataTest, EmptyHeaderValue) {
  initializeFilter(request_config_yaml);
  Http::TestHeaderMapImpl incoming_headers{{"X-VERSION", ""}};

  EXPECT_CALL(decoder_callbacks_, requestInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(incoming_headers, false));
}

/**
 * Header value too long.
 */
TEST_F(HeaderToMetadataTest, HeaderValueTooLong) {
  initializeFilter(request_config_yaml);
  Http::TestHeaderMapImpl incoming_headers{{"X-VERSION", std::string(101, 'x')}};

  EXPECT_CALL(decoder_callbacks_, requestInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(incoming_headers, false));
}

/**
 * Ignore the header's value, use a constant value.
 */
TEST_F(HeaderToMetadataTest, IgnoreHeaderValueUseConstant) {
  const std::string response_config_yaml = R"EOF(
response_rules:
  - header: x-something
    on_header_present:
      key: something
      value: else
      type: STRING
    remove: true
)EOF";
  initializeFilter(response_config_yaml);
  Http::TestHeaderMapImpl incoming_headers{{"x-something", "thing"}};
  std::map<std::string, std::string> expected = {{"something", "else"}};
  Http::TestHeaderMapImpl empty_headers;

  EXPECT_CALL(encoder_callbacks_, requestInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_,
              setDynamicMetadata("envoy.filters.http.header_to_metadata", MapEq(expected)));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(incoming_headers, false));
  EXPECT_EQ(empty_headers, incoming_headers);
}

/**
 * Rules with no on_header{present,missing} fields should be rejected.
 */
TEST_F(HeaderToMetadataTest, RejectInvalidRule) {
  const std::string config = R"EOF(
request_rules:
  - header: x-something
)EOF";
  auto expected = "header to metadata filter: rule for header 'x-something' has neither "
                  "`on_header_present` nor `on_header_missing` set";
  EXPECT_THROW_WITH_MESSAGE(initializeFilter(config), Envoy::EnvoyException, expected);
}

/**
 * Empty values not added to metadata.
 */
TEST_F(HeaderToMetadataTest, NoEmptyValues) {
  const std::string config = R"EOF(
request_rules:
  - header: x-version
    on_header_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
)EOF";
  initializeFilter(config);
  Http::TestHeaderMapImpl headers{{"x-version", ""}};

  EXPECT_CALL(decoder_callbacks_, requestInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
