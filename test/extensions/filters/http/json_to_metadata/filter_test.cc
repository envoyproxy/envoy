#include <memory>

#include "envoy/http/header_map.h"
#include "source/common/json/json_loader.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "source/extensions/filters/http/json_to_metadata/filter.h"
#include "test/common/stream_info/test_util.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonToMetadata {

MATCHER_P(MapEq, rhs, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(!rhs.empty());
  for (auto const& entry : rhs) {
    EXPECT_EQ(obj.fields().at(entry.first).string_value(), entry.second);
  }
  return true;
}

MATCHER_P2(MapEqType, rhs, getter, "") {
  const ProtobufWkt::Struct& obj = arg;
  EXPECT_TRUE(!rhs.empty());
  for (auto const& entry : rhs) {
    EXPECT_EQ(getter(obj.fields().at(entry.first)), entry.second);
  }
  return true;
}

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

class FilterTest : public testing::Test {
public:
  FilterTest() = default;

  const std::string request_config_yaml_ = R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_missing:
      metadata_namespace: envoy.lb
      key: version
      value: 'unknown'
      preserve_existing_metadata_value: true
    on_error:
      metadata_namespace: envoy.lb
      key: version
      value: 'error'
      preserve_existing_metadata_value: true
request_buffer_limit_bytes: 200
)EOF";

  void initializeFilter(const std::string& yaml) {
    envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata config;
    TestUtility::loadFromYaml(yaml, config);
    config_ = std::make_shared<FilterConfig>(config, *scope_.rootScope());
    filter_ = std::make_shared<Filter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  void sendData(const std::vector<std::string>& data_vector) {
    for (const auto& data : data_vector) {
      Buffer::OwnedImpl buffer(data);
      EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    }
  }

  uint64_t findCounter(const std::string& name) {
    const auto counter = TestUtility::findCounter(scope_, name);
    return counter != nullptr ? counter->value() : 0;
  }

  void setExistingMetadata() {
    auto& filter_metadata = *dynamic_metadata_.mutable_filter_metadata();

    (*filter_metadata["envoy.lb"].mutable_fields())["version"].set_string_value("existing");

    EXPECT_CALL(stream_info_, dynamicMetadata()).WillOnce(ReturnRef(dynamic_metadata_));
  }

  void
  testRequestWithBody(const std::string& body, bool end_stream = true,
                      Http::FilterDataStatus expected_result = Http::FilterDataStatus::Continue) {
    buffer_.add(body);
    ON_CALL(decoder_callbacks_, decodingBuffer()).WillByDefault(Return(&buffer_));

    EXPECT_EQ(expected_result, filter_->decodeData(buffer_, end_stream));
  }

  NiceMock<Stats::MockIsolatedStatsStore> scope_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  envoy::config::core::v3::Metadata dynamic_metadata_;
  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<Filter> filter_;
  Http::TestRequestHeaderMapImpl incoming_headers_{
      {":path", "/ping"}, {":method", "GET"}, {"Content-Type", "application/json"}};
  Http::TestResponseHeaderMapImpl outgoing_headers_{{":status", "200"}};
  Buffer::OwnedImpl buffer_;
};

TEST_F(FilterTest, BasicStringMatch) {
  initializeFilter(request_config_yaml_);
  std::string request_body =
      R"delimiter(
        {"version":"1.0.0",
        "messages":[
          {"role":"user","content":"content A"},
          {"role":"assistant","content":"content B"},
          {"role":"user","content":"content C"},
          {"role":"assistant","content":"content D"},
          {"role":"user","content":"content E"}],
        "stream":true})delimiter";
  std::map<std::string, std::string> expected = {{"version", "1.0.0"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, BasicBoolMatch) {
  initializeFilter(request_config_yaml_);
  std::string request_body = R"delimiter({"version":true})delimiter";
  std::map<std::string, bool> expected = {{"version", true}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(
      stream_info_,
      setDynamicMetadata("envoy.lb", MapEqType(expected, [](const ProtobufWkt::Value& value) {
                           return value.bool_value();
                         })));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, BasicIntegerMatch) {
  initializeFilter(request_config_yaml_);
  std::string request_body = R"delimiter({"version":1})delimiter";
  std::map<std::string, double> expected = {{"version", 1.0}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(
      stream_info_,
      setDynamicMetadata("envoy.lb", MapEqType(expected, [](const ProtobufWkt::Value& value) {
                           return value.number_value();
                         })));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, BasicDoubleMatch) {
  initializeFilter(request_config_yaml_);
  std::string request_body = R"delimiter({"version":1.0})delimiter";
  std::map<std::string, double> expected = {{"version", 1.0}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(
      stream_info_,
      setDynamicMetadata("envoy.lb", MapEqType(expected, [](const ProtobufWkt::Value& value) {
                           return value.number_value();
                         })));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, TrailerSupport) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
)EOF");
  std::string request_body = R"delimiter({"version":"good version"})delimiter";
  std::map<std::string, std::string> expected = {{"version", "good version"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body, false, Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  Http::TestRequestTrailerMapImpl trailers{{"some", "trailer"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, StringToString) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
)EOF");
  std::string request_body = R"delimiter({"version":"good version"})delimiter";
  std::map<std::string, std::string> expected = {{"version", "good version"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, StringToNumber) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: NUMBER
)EOF");
  std::string request_body = R"delimiter({"version":"123"})delimiter";
  std::map<std::string, double> expected = {{"version", 123.0}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(
      stream_info_,
      setDynamicMetadata("envoy.lb", MapEqType(expected, [](const ProtobufWkt::Value& value) {
                           return value.number_value();
                         })));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, BadStringToNumber) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: NUMBER
    on_missing:
      metadata_namespace: envoy.lb
      key: version
      value: 404
)EOF");
  std::string request_body = R"delimiter({"version":"invalid"})delimiter";
  std::map<std::string, double> expected = {{"version", 404}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(
      stream_info_,
      setDynamicMetadata("envoy.lb", MapEqType(expected, [](const ProtobufWkt::Value& value) {
                           return value.number_value();
                         })));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, NumberToString) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
)EOF");
  std::string request_body = R"delimiter({"version":220.0})delimiter";
  std::map<std::string, std::string> expected = {{"version", "220.000000"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, NumberToNumber) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: NUMBER
)EOF");
  std::string request_body = R"delimiter({"version":220.0})delimiter";
  std::map<std::string, double> expected = {{"version", 220.0}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(
      stream_info_,
      setDynamicMetadata("envoy.lb", MapEqType(expected, [](const ProtobufWkt::Value& value) {
                           return value.number_value();
                         })));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, IntegerToString) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
)EOF");
  std::string request_body = R"delimiter({"version":220})delimiter";
  std::map<std::string, std::string> expected = {{"version", "220"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, IntegerToNumber) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: NUMBER
)EOF");
  std::string request_body = R"delimiter({"version":220})delimiter";
  std::map<std::string, double> expected = {{"version", 220.0}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(
      stream_info_,
      setDynamicMetadata("envoy.lb", MapEqType(expected, [](const ProtobufWkt::Value& value) {
                           return value.number_value();
                         })));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, BoolToString) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
)EOF");
  std::string request_body = R"delimiter({"version":true})delimiter";
  std::map<std::string, std::string> expected = {{"version", "1"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, BoolToNumber) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: NUMBER
)EOF");
  std::string request_body = R"delimiter({"version":true})delimiter";
  std::map<std::string, double> expected = {{"version", 1}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(
      stream_info_,
      setDynamicMetadata("envoy.lb", MapEqType(expected, [](const ProtobufWkt::Value& value) {
                           return value.number_value();
                         })));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, OnPresentWithValueSet) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      value: "present"
)EOF");
  std::string request_body = R"delimiter({"version":"good version"})delimiter";
  std::map<std::string, std::string> expected = {{"version", "present"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, NoApplyOnMissingWhenPayloadIsPresent) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_missing:
      metadata_namespace: envoy.lb
      key: version
      value: "foo"
    on_error:
      metadata_namespace: envoy.lb
      key: version
      value: "foo"
)EOF");
  std::string request_body = R"delimiter({"version":"good version"})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));

  // No metadata is set from stream info.
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, DefaultNamespaceTest) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      key: version
)EOF");
  std::string request_body = R"delimiter({"version":"good version"})delimiter";
  std::map<std::string, std::string> expected = {{"version", "good version"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.filters.http.json_to_metadata", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, DecodeTwoDataStreams) {
  initializeFilter(request_config_yaml_);

  std::string request_body1 =
      R"delimiter(
        {"version":"1.0.0",
        "messages":[
          {"role":"user","content":"content A"},
          {"role":"assis)delimiter";
  std::string request_body2 =
      R"delimiter(tant","content":"content B"},
          {"role":"user","content":"content C"},
          {"role":"assistant","content":"content D"},
          {"role":"user","content":"content E"}],
        "stream":true})delimiter";
  std::map<std::string, std::string> expected = {{"version", "1.0.0"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body1, false, Http::FilterDataStatus::StopIterationAndBuffer);
  testRequestWithBody(request_body2);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, SecondLayerMatch) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: foo
    - key: bar
    on_present:
      metadata_namespace: envoy.lb
      key: baz
)EOF");
  std::string request_body =
      R"delimiter(
        {"foo":{
          "bar":"value"
          }
        })delimiter";
  std::map<std::string, std::string> expected = {{"baz", "value"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, OnMissingFirstLayer) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: foo
    - key: bar
    on_present:
      metadata_namespace: envoy.lb
      key: baz
)EOF");
  std::string request_body =
      R"delimiter(
        {"nah":{
          "bar":"Scotty, beam me up!"
          }
        })delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));

  // No metadata is set from stream info.
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, OnMissingSecondLayer) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: foo
    - key: bar
    on_present:
      metadata_namespace: envoy.lb
      key: baz
)EOF");
  std::string request_body =
      R"delimiter(
        {"foo":{
          "nah":"James, James, Morrison, Morrison, weather-beaten, off-key"
          }
        })delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));

  // No metadata is set from stream info.
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, OnMissingForArray) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: foo
    - key: bar
    on_present:
      metadata_namespace: envoy.lb
      key: baz
)EOF");
  std::string request_body =
      R"delimiter(
        {"foo":[
            { "bar":"Shaken, not stirred for Sean."}
          ]
        })delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));

  // No metadata is set from stream info.
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, NoRequestContentType) {
  initializeFilter(request_config_yaml_);

  Http::TestRequestHeaderMapImpl mismatched_incoming_headers{{":path", "/ping"},
                                                             {":method", "GET"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(mismatched_incoming_headers, false));
  testRequestWithBody("{}");

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, MismatchedRequestContentType) {
  initializeFilter(request_config_yaml_);

  Http::TestRequestHeaderMapImpl mismatched_incoming_headers{
      {":path", "/ping"}, {":method", "GET"}, {"Content-Type", "application/not-a-json"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(mismatched_incoming_headers, false));
  testRequestWithBody("Peter picked a peck of pickled peppers");

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, NoRequestBody) {
  initializeFilter(request_config_yaml_);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(incoming_headers_, true));

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, EmptyPayloadValue) {
  initializeFilter(request_config_yaml_);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  testRequestWithBody("");

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, InvalidJsonPayload) {
  initializeFilter(request_config_yaml_);
  // missing right-most curly brace
  std::string request_body =
      R"delimiter(
      {"version":"1.0.0",
      "messages":[
        {"role":"user","content":"content A"},
        {"role":"assistant","content":"content B"},
        {"role":"user","content":"content C"},
        {"role":"assistant","content":"content D"},
        {"role":"user","content":"content E"}],
      "stream":true)delimiter";
  std::map<std::string, std::string> expected = {{"version", "error"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 1);
}

// TODO(kuochunghsu): Planned to support trimming.
TEST_F(FilterTest, InvalidJsonForAdditionalPrefixSuffix) {
  initializeFilter(request_config_yaml_);
  // missing right-most curly brace
  std::string request_body =
      R"delimiter(data: {"id":"ID","object":"chat.completion.chunk","created":1686100940,"version":"1.0.0-0301"}\n\ndata: [DONE]\n\n)delimiter";
  std::map<std::string, std::string> expected = {{"version", "error"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 1);
}

TEST_F(FilterTest, EmptyStringValue) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
)EOF");

  std::string request_body = R"delimiter({"version":""})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));

  // No metadata is set from stream info.
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);
  testRequestWithBody(request_body);

  Buffer::OwnedImpl buffer(request_body);
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, PayloadValueTooLong) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_missing:
      metadata_namespace: envoy.lb
      key: version
      value: 'unknown'
)EOF");

  std::string value(MAX_PAYLOAD_VALUE_LEN + 1, 'z');
  std::string request_body =
      absl::StrCat(R"delimiter({"version":")delimiter", value, R"delimiter("})delimiter");

  std::map<std::string, std::string> expected = {{"version", "unknown"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  Buffer::OwnedImpl buffer(request_body);
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, PayloadValueTooLongValueTypeString) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
    on_missing:
      metadata_namespace: envoy.lb
      key: version
      value: 'unknown'
)EOF");

  std::string value(MAX_PAYLOAD_VALUE_LEN + 1, 'z');
  std::string request_body =
      absl::StrCat(R"delimiter({"version":")delimiter", value, R"delimiter("})delimiter");

  std::map<std::string, std::string> expected = {{"version", "unknown"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  Buffer::OwnedImpl buffer(request_body);
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, MissingMetadataKeyAndFallbackValue) {
  initializeFilter(request_config_yaml_);
  std::string request_body =
      R"delimiter(
        {"messages":[
          {"role":"user","content":"content A"},
          {"role":"assistant","content":"content B"},
          {"role":"user","content":"content C"},
          {"role":"assistant","content":"content D"},
          {"role":"user","content":"content E"}],
        "stream":true})delimiter";
  std::map<std::string, std::string> expected = {{"version", "unknown"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, MissingMetadataKeyWithNoFallbackValue) {
  const std::string yaml = R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
)EOF";

  initializeFilter(yaml);
  std::string request_body =
      R"delimiter(
    {"messages":[
      {"role":"user","content":"content A"},
      {"role":"assistant","content":"content B"},
      {"role":"user","content":"content C"},
      {"role":"assistant","content":"content D"},
      {"role":"user","content":"content E"}],
    "stream":true})delimiter";
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  // No metadata is set from stream info.
  EXPECT_CALL(decoder_callbacks_, streamInfo()).Times(0);
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, MissingMetadataKeyWithExistingMetadata) {
  initializeFilter(request_config_yaml_);
  std::string request_body =
      R"delimiter(
    {"messages":[
      {"role":"user","content":"content A"},
      {"role":"assistant","content":"content B"},
      {"role":"user","content":"content C"},
      {"role":"assistant","content":"content D"},
      {"role":"user","content":"content E"}],
    "stream":true})delimiter";

  setExistingMetadata();

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));

  // No metadata is set from stream info.
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, TooLargeRequestBody) {
  initializeFilter(request_config_yaml_);
  std::string request_body(201, 'z');
  std::map<std::string, std::string> expected = {{"version", "error"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  Buffer::OwnedImpl buffer(request_body);
  testRequestWithBody(request_body, false, Http::FilterDataStatus::Continue);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, DefaultBufferLimitAccepted) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
)EOF");

  std::string value(1024 * 1024 - 40, 'z');
  std::string request_body =
      absl::StrCat(R"delimiter({"version":"good version", "placeholder":")delimiter", value,
                   R"delimiter("})delimiter");

  EXPECT_EQ(1024 * 1024, request_body.length());

  std::map<std::string, std::string> expected = {{"version", "good version"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  Buffer::OwnedImpl buffer(request_body);
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, DefaultBufferLimitRejected) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_error:
      metadata_namespace: envoy.lb
      key: version
      value: 'error'
)EOF");
  std::string request_body(1024 * 1024 + 1, 'z');
  std::map<std::string, std::string> expected = {{"version", "error"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  Buffer::OwnedImpl buffer(request_body);
  testRequestWithBody(request_body, false, Http::FilterDataStatus::Continue);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, MultipleRules) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
  - selectors:
    - key: id
    on_present:
      metadata_namespace: envoy.lb
      key: id
)EOF");
  std::string request_body = R"delimiter({"version":"good version", "id":"beautiful id"})delimiter";
  std::map<std::string, std::string> expected = {{"version", "good version"},
                                                 {"id", "beautiful id"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, MultipleRulesInSamePath) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
  - selectors:
    - key: version
    on_present:
      metadata_namespace: another.namespace
      key: version
)EOF");
  std::string request_body = R"delimiter({"version":"good version", "id":"beautiful id"})delimiter";
  std::map<std::string, std::string> expected = {{"version", "good version"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  EXPECT_CALL(stream_info_, setDynamicMetadata("another.namespace", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, MultipleRulesSecondLayer) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
  - selectors:
    - key: messages
    - key: foo
    on_present:
      metadata_namespace: envoy.lb
      key: foo
  - selectors:
    - key: messages
    - key: baz
    on_present:
      metadata_namespace: envoy.lb
      key: baz
)EOF");
  std::string request_body = R"delimiter({"version":"good version", "messages":{
      "foo":"bar",
      "baz":"qux"
    }
  })delimiter";
  std::map<std::string, std::string> expected = {
      {"version", "good version"}, {"foo", "bar"}, {"baz", "qux"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, CustomRequestAllowContentTypeAccepted) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
request_allow_content_types:
  - "application/better-json"
)EOF");
  std::string request_body = R"delimiter({"version":"good version"})delimiter";
  std::map<std::string, std::string> expected = {{"version", "good version"}};

  Http::TestRequestHeaderMapImpl matched_incoming_headers{
      {":path", "/ping"}, {":method", "GET"}, {"Content-Type", "application/better-json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(matched_incoming_headers, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, CustomRequestAllowContentTypeRejected) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
request_allow_content_types:
  - "application/non-json"
)EOF");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(incoming_headers_, false));

  testRequestWithBody("{}");

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

TEST_F(FilterTest, RequestAllowEmptyContentType) {
  initializeFilter(R"EOF(
request_rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
request_allow_empty_content_type: true
)EOF");
  std::string request_body = R"delimiter({"version":"good version"})delimiter";
  std::map<std::string, std::string> expected = {{"version", "good version"}};

  Http::TestRequestHeaderMapImpl matched_incoming_headers{{":path", "/ping"}, {":method", "GET"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(matched_incoming_headers, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", MapEq(expected)));
  testRequestWithBody(request_body);

  EXPECT_EQ(findCounter("json_to_metadata.rq_success"), 1);
  EXPECT_EQ(findCounter("json_to_metadata.rq_mismatched_content_type"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_no_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_too_large_body"), 0);
  EXPECT_EQ(findCounter("json_to_metadata.rq_invalid_json_body"), 0);
}

} // namespace JsonToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
