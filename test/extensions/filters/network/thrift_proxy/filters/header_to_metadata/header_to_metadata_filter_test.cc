#include <string>

#include "source/common/common/base64.h"
#include "source/extensions/filters/network/thrift_proxy/filters/header_to_metadata/header_to_metadata_filter.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace HeaderToMetadataFilter {

namespace {

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

} // namespace

using namespace Envoy::Extensions::NetworkFilters;

class HeaderToMetadataTest : public testing::Test {
public:
  void initializeFilter(const std::string& yaml) {
    envoy::extensions::filters::network::thrift_proxy::filters::header_to_metadata::v3::
        HeaderToMetadata proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    const auto& filter_config = std::make_shared<Config>(proto_config);
    filter_ = std::make_shared<HeaderToMetadataFilter>(filter_config);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  NiceMock<ThriftProxy::ThriftFilters::MockDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> req_info_;
  std::shared_ptr<HeaderToMetadataFilter> filter_;
};

TEST_F(HeaderToMetadataTest, BasicRequestTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-version
    on_present:
      metadata_namespace: envoy.lb
      key: version
)EOF";
  initializeFilter(request_config_yaml);
  const std::map<std::string, std::string> expected = {{"version", "0xdeadbeef"}};
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-VERSION"), "0xdeadbeef");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

TEST_F(HeaderToMetadataTest, DefaultNamespaceTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-version
    on_present:
      key: version
)EOF";
  initializeFilter(request_config_yaml);
  const std::map<std::string, std::string> expected = {{"version", "0xdeadbeef"}};
  EXPECT_CALL(req_info_,
              setDynamicMetadata("envoy.filters.thrift.header_to_metadata", MapEq(expected)));

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-VERSION"), "0xdeadbeef");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

TEST_F(HeaderToMetadataTest, ReplaceValueTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-replace
    on_present:
      metadata_namespace: envoy.lb
      key: replace
      value: world
)EOF";
  initializeFilter(request_config_yaml);
  const std::map<std::string, std::string> expected = {{"replace", "world"}};
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-replace"), "hello");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

TEST_F(HeaderToMetadataTest, SubstituteValueTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-sub
    on_present:
      metadata_namespace: envoy.lb
      key: subbed
      regex_value_rewrite:
        pattern:
          google_re2: {}
          regex: "^hello (\\w+)?.*$"
        substitution: "\\1"
)EOF";
  initializeFilter(request_config_yaml);
  const std::map<std::string, std::string> expected = {{"subbed", "world"}};
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-sub"), "hello world!!!!!");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

TEST_F(HeaderToMetadataTest, NoMatchSubstituteValueTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-sub
    on_present:
      metadata_namespace: envoy.lb
      key: subbed
      regex_value_rewrite:
        pattern:
          google_re2: {}
          regex: "^hello (\\w+)?.*$"
        substitution: "\\1"
)EOF";
  initializeFilter(request_config_yaml);
  const std::map<std::string, std::string> expected = {{"subbed", "does not match"}};
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-sub"), "does not match");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

/**
 * Test empty value doesn't get written to metadata.
 */
TEST_F(HeaderToMetadataTest, SubstituteEmptyValueTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-sub
    on_present:
      metadata_namespace: envoy.lb
      key: subbed
      regex_value_rewrite:
        pattern:
          google_re2: {}
          regex: "^hello (\\w+)?.*$"
        substitution: "\\1"
)EOF";
  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-sub"), "hello !!!!!");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

/**
 * Test the invalid value won't get written as a number.
 */
TEST_F(HeaderToMetadataTest, NumberTypeTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-number
    on_present:
      metadata_namespace: envoy.lb
      key: number
      type: NUMBER
)EOF";
  initializeFilter(request_config_yaml);
  std::map<std::string, int> expected = {{"number", 1}};
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEqNum(expected)));

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-Number"), "1");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

/**
 * Test the value gets written as a number.
 */
TEST_F(HeaderToMetadataTest, BadNumberTypeTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-number
    on_present:
      metadata_namespace: envoy.lb
      key: number
      type: NUMBER
)EOF";
  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-Number"), "invalid");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

/**
 * Test the Base64 encoded value gets written as a string.
 */
TEST_F(HeaderToMetadataTest, StringTypeInBase64UrlTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-base64
    on_present:
      metadata_namespace: envoy.lb
      key: base64_key
      type: STRING
      encode: BASE64
)EOF";
  initializeFilter(request_config_yaml);
  std::string data = "Non-ascii-characters";
  const std::map<std::string, std::string> expected = {{"base64_key", data}};
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));

  const auto encoded = Base64::encode(data.c_str(), data.size());
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-Base64"), encoded);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

/**
 * Test the Base64 encoded protobuf value gets written as a protobuf value.
 */
TEST_F(HeaderToMetadataTest, ProtobufValueTypeInBase64UrlTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-proto-base64
    on_present:
      metadata_namespace: envoy.lb
      key: proto_key
      type: PROTOBUF_VALUE
      encode: BASE64
)EOF";
  initializeFilter(request_config_yaml);

  ProtobufWkt::Value value;
  auto* s = value.mutable_struct_value();

  ProtobufWkt::Value v;
  v.set_string_value("blafoo");
  (*s->mutable_fields())["k1"] = v;
  v.set_number_value(2019.07);
  (*s->mutable_fields())["k2"] = v;
  v.set_bool_value(true);
  (*s->mutable_fields())["k3"] = v;

  std::map<std::string, ProtobufWkt::Value> expected = {{"proto_key", value}};
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEqValue(expected)));

  std::string data;
  ASSERT_TRUE(value.SerializeToString(&data));
  const auto encoded = Base64::encode(data.c_str(), data.size());
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-Proto-Base64"), encoded);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

/**
 * Test bad Base64 encoding is not written.
 */
TEST_F(HeaderToMetadataTest, ProtobufValueTypeInBadBase64UrlTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-bad-base64
    on_present:
      key: proto_key
      type: PROTOBUF_VALUE
      encode: BASE64
)EOF";
  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-Bad-Base64"), "invalid");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

/**
 * Test the bad protobuf value is not written.
 */
TEST_F(HeaderToMetadataTest, BadProtobufValueTypeInBase64UrlTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-bad-proto
    on_present:
      key: proto_key
      type: PROTOBUF_VALUE
      encode: BASE64
)EOF";
  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);

  std::string data = "invalid";
  const auto encoded = Base64::encode(data.c_str(), data.size());
  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-Bad-Proto"), encoded);
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

/*
 * Set configured value when header is missing.
 */
TEST_F(HeaderToMetadataTest, SetMissingValueTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-no-exist
    on_missing:
      metadata_namespace: envoy.lb
      key: set
      value: hi
)EOF";
  initializeFilter(request_config_yaml);
  const std::map<std::string, std::string> expected = {{"set", "hi"}};
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

/**
 * Missing case is not executed when header is present.
 */
TEST_F(HeaderToMetadataTest, NoMissingWhenHeaderIsPresent) {
  const std::string config = R"EOF(
request_rules:
  - header: x-exist
    on_missing:
      metadata_namespace: envoy.lb
      key: version
      value: hi
)EOF";
  initializeFilter(config);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-Exist"), "hello");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

TEST_F(HeaderToMetadataTest, RemoveHeaderTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-remove
    on_present:
      metadata_namespace: envoy.lb
      key: remove
      value: hello
    remove: true
  - header: x-keep
    on_present:
      metadata_namespace: envoy.lb
      key: keep
      value: world
)EOF";
  initializeFilter(request_config_yaml);
  const std::map<std::string, std::string> expected = {{"remove", "hello"}, {"keep", "world"}};
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-REMOVE"),
                                     "replaced in metadata then removed from headers");
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-KEEP"), "remains");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  Http::TestRequestHeaderMapImpl headers{metadata->requestHeaders()};
  EXPECT_EQ("", headers.get_(Http::LowerCaseString("X-REMOVE")));
  EXPECT_EQ("remains", headers.get_(Http::LowerCaseString("X-KEEP")));
  filter_->onDestroy();
}

/**
 * No header value does not set any metadata.
 */
TEST_F(HeaderToMetadataTest, EmptyHeaderValue) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-version
    on_present:
      metadata_namespace: envoy.lb
      key: version
)EOF";
  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-VERSION"), "");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

/**
 * Header value too long does not set header value as metadata.
 */
TEST_F(HeaderToMetadataTest, HeaderValueTooLong) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-version
    on_present:
      metadata_namespace: envoy.lb
      key: version
)EOF";
  initializeFilter(request_config_yaml);
  EXPECT_CALL(req_info_, setDynamicMetadata(_, _)).Times(0);

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  auto length = MAX_HEADER_VALUE_LEN + 1;
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-VERSION"), std::string(length, 'x'));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

TEST_F(HeaderToMetadataTest, MultipleRulesTest) {
  const std::string request_config_yaml = R"EOF(
request_rules:
  - header: x-no-exist
    on_missing:
      metadata_namespace: envoy.lb
      key: set
      value: hello
  - header: x-replace
    on_present:
      metadata_namespace: envoy.lb
      key: replace
      value: world
)EOF";
  initializeFilter(request_config_yaml);
  const std::map<std::string, std::string> expected = {{"set", "hello"}, {"replace", "world"}};
  EXPECT_CALL(req_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));

  auto metadata = std::make_shared<Extensions::NetworkFilters::ThriftProxy::MessageMetadata>();
  metadata->requestHeaders().setCopy(Http::LowerCaseString("X-REPLACE"), "should be replaced");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(req_info_));
  EXPECT_EQ(ThriftProxy::FilterStatus::Continue, filter_->transportBegin(metadata));
  filter_->onDestroy();
}

} // namespace HeaderToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
