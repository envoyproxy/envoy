#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.h"

#include "common/common/base64.h"
#include "common/http/header_map_impl.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/header_to_metadata/header_to_metadata_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {
namespace {

class HeaderToMetadataTest : public testing::Test {
public:
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
    envoy::extensions::filters::http::header_to_metadata::v3::Config config;
    TestUtility::loadFromYaml(yaml, config);
    config_.reset(new Config(config));
    filter_.reset(new HeaderToMetadataFilter(config_));
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  ConfigSharedPtr config_;
  std::shared_ptr<HeaderToMetadataFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info_;
};

MATCHER_P(MapEq, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(!rhs.empty());
  for (auto const& entry : rhs) {
    EXPECT_EQ(obj.fields().at(entry.first).string_value(), entry.second);
  }
  return true;
}

MATCHER_P(MapEqNum, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(!rhs.empty());
  for (auto const& entry : rhs) {
    EXPECT_EQ(obj.fields().at(entry.first).number_value(), entry.second);
  }
  return true;
}

MATCHER_P(MapEqValue, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(!rhs.empty());
  for (auto const& entry : rhs) {
    EXPECT_TRUE(TestUtility::protoEqual(obj.fields().at(entry.first), entry.second));
  }
  return true;
}

/**
 * Basic use-case.
 */
TEST_F(HeaderToMetadataTest, BasicRequestTest) {
  initializeFilter(request_config_yaml);
  Http::TestRequestHeaderMapImpl incoming_headers{{"X-VERSION", "0xdeadbeef"}};
  std::map<std::string, std::string> expected = {{"version", "0xdeadbeef"}};

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(incoming_headers, false));
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  Buffer::OwnedImpl data("data");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  Http::TestRequestTrailerMapImpl incoming_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(incoming_trailers));
  filter_->onDestroy();
}

/**
 * X-version not set, the on missing value should be set.
 */
TEST_F(HeaderToMetadataTest, DefaultEndpointsTest) {
  initializeFilter(request_config_yaml);
  Http::TestRequestHeaderMapImpl incoming_headers{{"X-FOO", "bar"}};
  std::map<std::string, std::string> expected = {{"default", "true"}};

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
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
  Http::TestResponseHeaderMapImpl incoming_headers{{"x-authenticated", "1"}};
  std::map<std::string, std::string> expected = {{"auth", "1"}};
  Http::TestHeaderMapImpl empty_headers;

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_,
              setDynamicMetadata("envoy.filters.http.header_to_metadata", MapEq(expected)));
  Http::TestResponseHeaderMapImpl continue_response{{":status", "100"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encode100ContinueHeaders(continue_response));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(incoming_headers, false));
  EXPECT_EQ(empty_headers, incoming_headers);
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));
  Buffer::OwnedImpl data("data");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, false));
  Http::TestResponseTrailerMapImpl incoming_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(incoming_trailers));
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
  Http::TestResponseHeaderMapImpl incoming_headers{{"x-authenticated", "1"}};
  std::map<std::string, int> expected = {{"auth", 1}};
  Http::TestHeaderMapImpl empty_headers;

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_,
              setDynamicMetadata("envoy.filters.http.header_to_metadata", MapEqNum(expected)));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(incoming_headers, false));
}

/**
 * Test the Base64 encoded value gets written as a string.
 */
TEST_F(HeaderToMetadataTest, StringTypeInBase64UrlTest) {
  const std::string response_config_yaml = R"EOF(
response_rules:
  - header: x-authenticated
    on_header_present:
      key: auth
      type: STRING
      encode: BASE64
)EOF";
  initializeFilter(response_config_yaml);
  std::string data = "Non-ascii-characters";
  const auto encoded = Base64::encode(data.c_str(), data.size());
  Http::TestResponseHeaderMapImpl incoming_headers{{"x-authenticated", encoded}};
  std::map<std::string, std::string> expected = {{"auth", data}};
  Http::TestHeaderMapImpl empty_headers;

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_,
              setDynamicMetadata("envoy.filters.http.header_to_metadata", MapEq(expected)));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(incoming_headers, false));
}

/**
 * Test the Base64 encoded protobuf value gets written as a protobuf value.
 */
TEST_F(HeaderToMetadataTest, ProtobufValueTypeInBase64UrlTest) {
  const std::string response_config_yaml = R"EOF(
response_rules:
  - header: x-authenticated
    on_header_present:
      key: auth
      type: PROTOBUF_VALUE
      encode: BASE64
)EOF";
  initializeFilter(response_config_yaml);

  ProtobufWkt::Value value;
  auto* s = value.mutable_struct_value();

  ProtobufWkt::Value v;
  v.set_string_value("blafoo");
  (*s->mutable_fields())["k1"] = v;
  v.set_number_value(2019.07);
  (*s->mutable_fields())["k2"] = v;
  v.set_bool_value(true);
  (*s->mutable_fields())["k3"] = v;

  std::string data;
  ASSERT_TRUE(value.SerializeToString(&data));
  const auto encoded = Base64::encode(data.c_str(), data.size());
  Http::TestResponseHeaderMapImpl incoming_headers{{"x-authenticated", encoded}};
  std::map<std::string, ProtobufWkt::Value> expected = {{"auth", value}};

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_,
              setDynamicMetadata("envoy.filters.http.header_to_metadata", MapEqValue(expected)));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(incoming_headers, false));
}

/**
 * Test bad Base64 encoding is not written.
 */
TEST_F(HeaderToMetadataTest, ProtobufValueTypeInBadBase64UrlTest) {
  const std::string response_config_yaml = R"EOF(
response_rules:
  - header: x-authenticated
    on_header_present:
      key: auth
      type: PROTOBUF_VALUE
      encode: BASE64
)EOF";
  initializeFilter(response_config_yaml);
  Http::TestResponseHeaderMapImpl incoming_headers{{"x-authenticated", "invalid"}};

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(incoming_headers, false));
}

/**
 * Test the bad protobuf value is not written.
 */
TEST_F(HeaderToMetadataTest, BadProtobufValueTypeInBase64UrlTest) {
  const std::string response_config_yaml = R"EOF(
response_rules:
  - header: x-authenticated
    on_header_present:
      key: auth
      type: PROTOBUF_VALUE
      encode: BASE64
)EOF";
  initializeFilter(response_config_yaml);
  std::string data = "invalid";
  const auto encoded = Base64::encode(data.c_str(), data.size());
  Http::TestResponseHeaderMapImpl incoming_headers{{"x-authenticated", encoded}};

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
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
  Http::TestRequestHeaderMapImpl incoming_headers;

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
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
  Http::TestRequestHeaderMapImpl incoming_headers{
      {"X-VERSION", "v4.0"},
      {"X-PYTHON-VERSION", "3.7"},
      {"X-IGNORE", "nothing"},
  };
  std::map<std::string, std::string> expected = {{"version", "v4.0"}, {"python_version", "3.7"}};

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(incoming_headers, false));
}

/**
 * No header value.
 */
TEST_F(HeaderToMetadataTest, EmptyHeaderValue) {
  initializeFilter(request_config_yaml);
  Http::TestRequestHeaderMapImpl incoming_headers{{"X-VERSION", ""}};

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(incoming_headers, false));
}

/**
 * Header value too long.
 */
TEST_F(HeaderToMetadataTest, HeaderValueTooLong) {
  initializeFilter(request_config_yaml);
  auto length = Envoy::Extensions::HttpFilters::HeaderToMetadataFilter::MAX_HEADER_VALUE_LEN + 1;
  Http::TestRequestHeaderMapImpl incoming_headers{{"X-VERSION", std::string(length, 'x')}};

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
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
  Http::TestResponseHeaderMapImpl incoming_headers{{"x-something", "thing"}};
  std::map<std::string, std::string> expected = {{"something", "else"}};
  Http::TestHeaderMapImpl empty_headers;

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
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
  Http::TestRequestHeaderMapImpl headers{{"x-version", ""}};

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

} // namespace
} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
