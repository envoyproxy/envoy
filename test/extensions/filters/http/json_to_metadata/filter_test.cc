#include <memory>

#include "envoy/http/header_map.h"

#include "source/common/json/json_loader.h"
#include "source/extensions/filters/http/json_to_metadata/filter.h"
#include "source/extensions/filters/http/well_known_names.h"

#include "test/common/stream_info/test_util.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/struct_matchers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonToMetadata {

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::UnorderedElementsAre;

class FilterTest : public testing::Test {
public:
  FilterTest() = default;

  const std::string config_yaml_ = R"EOF(
request_rules:
  rules:
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
response_rules:
  rules:
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
)EOF";

  void initializeFilter(const std::string& yaml) {
    envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata config;
    TestUtility::loadFromYaml(yaml, config);
    config_ = *FilterConfig::create(config, *scope_.rootScope(), regex_engine_, false);
    filter_ = std::make_shared<Filter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  std::shared_ptr<FilterConfig> createConfig(const std::string& yaml, bool per_route = false) {
    envoy::extensions::filters::http::json_to_metadata::v3::JsonToMetadata config;
    TestUtility::loadFromYaml(yaml, config);
    return *FilterConfig::create(config, *scope_.rootScope(), regex_engine_, per_route);
  }

  void sendData(const std::vector<std::string>& data_vector) {
    for (const auto& data : data_vector) {
      Buffer::OwnedImpl buffer(data);
      EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
    }
  }

  uint64_t getCounterValue(const std::string& name) {
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

  void
  testResponseWithBody(const std::string& body, bool end_stream = true,
                       Http::FilterDataStatus expected_result = Http::FilterDataStatus::Continue) {
    buffer_.add(body);
    ON_CALL(encoder_callbacks_, encodingBuffer()).WillByDefault(Return(&buffer_));

    EXPECT_EQ(expected_result, filter_->encodeData(buffer_, end_stream));
  }

  Regex::GoogleReEngine regex_engine_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  envoy::config::core::v3::Metadata dynamic_metadata_;
  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<Filter> filter_;
  Buffer::OwnedImpl buffer_;
  Http::TestRequestHeaderMapImpl incoming_headers_{
      {":path", "/ping"}, {":method", "GET"}, {"Content-Type", "application/json"}};
  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"},
                                                    {"Content-Type", "application/json"}};
};

TEST_F(FilterTest, BasicStringMatch) {
  initializeFilter(config_yaml_);
  const std::string request_body =
      R"delimiter(
        {"version":"1.0.0",
        "messages":[
          {"role":"user","content":"content A"},
          {"role":"assistant","content":"content B"},
          {"role":"user","content":"content C"},
          {"role":"assistant","content":"content D"},
          {"role":"user","content":"content E"}],
        "stream":true})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "1.0.0")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, BasicResponseStringMatch) {
  initializeFilter(config_yaml_);
  const std::string response_body =
      R"delimiter(
        {"version":"1.0.0",
        "messages":[
          {"role":"user","content":"content A"},
          {"role":"assistant","content":"content B"},
          {"role":"user","content":"content C"},
          {"role":"assistant","content":"content D"},
          {"role":"user","content":"content E"}],
        "stream":true})delimiter";

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "1.0.0")))));
  testResponseWithBody(response_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.resp.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.invalid_json_body"), 0);
}

TEST_F(FilterTest, BasicBoolMatch) {
  initializeFilter(config_yaml_);
  const std::string request_body = R"delimiter({"version":true})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                               IsStructBool("version", true)))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, BasicIntegerMatch) {
  initializeFilter(config_yaml_);
  const std::string request_body = R"delimiter({"version":1})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                               IsStructNumber("version", 1.0)))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, BasicDoubleMatch) {
  initializeFilter(config_yaml_);
  const std::string request_body = R"delimiter({"version":1.0})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                               IsStructNumber("version", 1.0)))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, TrailerSupport) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
)EOF");
  const std::string request_body = R"delimiter({"version":"good version"})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "good version")))));
  testRequestWithBody(request_body, false, Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  Http::TestRequestTrailerMapImpl trailers{{"some", "trailer"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, ResponseTrailerSupport) {
  initializeFilter(R"EOF(
response_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
)EOF");
  const std::string response_body = R"delimiter({"version":"good version"})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "good version")))));
  testResponseWithBody(response_body, false, Http::FilterDataStatus::StopIterationAndBuffer);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));

  Http::TestResponseTrailerMapImpl trailers{{"some", "trailer"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));

  EXPECT_EQ(getCounterValue("json_to_metadata.resp.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.invalid_json_body"), 0);
}

TEST_F(FilterTest, StringToString) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
)EOF");
  const std::string request_body = R"delimiter({"version":"good version"})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "good version")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, StringToNumber) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: NUMBER
)EOF");
  const std::string request_body = R"delimiter({"version":"123"})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                               IsStructNumber("version", 123.0)))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, BadStringToNumber) {
  initializeFilter(R"EOF(
request_rules:
  rules:
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
  const std::string request_body = R"delimiter({"version":"invalid"})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                               IsStructNumber("version", 404)))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, NumberToString) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
)EOF");
  const std::string request_body = R"delimiter({"version":220.0})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "220.000000")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, NumberToNumber) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: NUMBER
)EOF");
  const std::string request_body = R"delimiter({"version":220.0})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                               IsStructNumber("version", 220.0)))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, IntegerToString) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
)EOF");
  const std::string request_body = R"delimiter({"version":220})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                               IsStructString("version", "220")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, IntegerToNumber) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: NUMBER
)EOF");
  const std::string request_body = R"delimiter({"version":220})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                               IsStructNumber("version", 220.0)))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, BoolToString) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
)EOF");
  const std::string request_body = R"delimiter({"version":true})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                               IsStructString("version", "1")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, BoolToNumber) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: NUMBER
)EOF");
  const std::string request_body = R"delimiter({"version":true})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                               IsStructNumber("version", 1)))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, OnPresentWithValueSet) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      value: "present"
)EOF");
  const std::string request_body = R"delimiter({"version":"good version"})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "present")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, NoApplyOnMissingWhenPayloadIsPresent) {
  initializeFilter(R"EOF(
request_rules:
  rules:
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
  const std::string request_body = R"delimiter({"version":"good version"})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));

  // No metadata is set from stream info.
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, DefaultNamespaceTest) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      key: version
)EOF");
  const std::string request_body = R"delimiter({"version":"good version"})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata(HttpFilterNames::get().JsonToMetadata,
                                               HasStructFields(UnorderedElementsAre(
                                                   IsStructString("version", "good version")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, DecodeTwoDataStreams) {
  initializeFilter(config_yaml_);

  const std::string request_body1 =
      R"delimiter(
        {"version":"1.0.0",
        "messages":[
          {"role":"user","content":"content A"},
          {"role":"assis)delimiter";
  const std::string request_body2 =
      R"delimiter(tant","content":"content B"},
          {"role":"user","content":"content C"},
          {"role":"assistant","content":"content D"},
          {"role":"user","content":"content E"}],
        "stream":true})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "1.0.0")))));
  testRequestWithBody(request_body1, false, Http::FilterDataStatus::StopIterationAndBuffer);
  testRequestWithBody(request_body2);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, EncodeTwoDataStreams) {
  initializeFilter(config_yaml_);

  const std::string response_body1 =
      R"delimiter(
        {"version":"1.0.0",
        "messages":[
          {"role":"user","content":"content A"},
          {"role":"assis)delimiter";
  const std::string response_body2 =
      R"delimiter(tant","content":"content B"},
          {"role":"user","content":"content C"},
          {"role":"assistant","content":"content D"},
          {"role":"user","content":"content E"}],
        "stream":true})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "1.0.0")))));
  testResponseWithBody(response_body1, false, Http::FilterDataStatus::StopIterationAndBuffer);
  testResponseWithBody(response_body2);

  EXPECT_EQ(getCounterValue("json_to_metadata.resp.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.invalid_json_body"), 0);
}

TEST_F(FilterTest, SecondLayerMatch) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: foo
    - key: bar
    on_present:
      metadata_namespace: envoy.lb
      key: baz
)EOF");
  const std::string request_body =
      R"delimiter(
        {"foo":{
          "bar":"value"
          }
        })delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_, setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                               IsStructString("baz", "value")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, OnMissingFirstLayer) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: foo
    - key: bar
    on_present:
      metadata_namespace: envoy.lb
      key: baz
)EOF");
  const std::string request_body =
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

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, OnMissingSecondLayer) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: foo
    - key: bar
    on_present:
      metadata_namespace: envoy.lb
      key: baz
)EOF");
  const std::string request_body =
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

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, OnMissingForArray) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: foo
    - key: bar
    on_present:
      metadata_namespace: envoy.lb
      key: baz
)EOF");
  const std::string request_body =
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

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, OnMissingSecondLayerString) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: foo
    - key: bar
    on_present:
      metadata_namespace: envoy.lb
      key: baz
    on_missing:
      metadata_namespace: envoy.lb
      key: version
      value: "missing"
)EOF");
  const std::string request_body =
      R"delimiter(
        {"foo": "James, James, Morrison, Morrison, weather-beaten, off-key"})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "missing")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, NoRequestContentType) {
  initializeFilter(config_yaml_);

  Http::TestRequestHeaderMapImpl mismatched_incoming_headers{{":path", "/ping"},
                                                             {":method", "GET"}};

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "error")))));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(mismatched_incoming_headers, false));
  testRequestWithBody("{}");

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, MismatchedRequestContentType) {
  initializeFilter(config_yaml_);

  Http::TestRequestHeaderMapImpl mismatched_incoming_headers{
      {":path", "/ping"}, {":method", "GET"}, {"Content-Type", "application/not-a-json"}};

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "error")))));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(mismatched_incoming_headers, false));
  testRequestWithBody("Peter picked a peck of pickled peppers");

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, NoRequestBody) {
  initializeFilter(config_yaml_);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(incoming_headers_, true));

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, NoResponseBody) {
  initializeFilter(config_yaml_);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));

  EXPECT_EQ(getCounterValue("json_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.no_body"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.invalid_json_body"), 0);
}

TEST_F(FilterTest, EmptyPayloadValue) {
  initializeFilter(config_yaml_);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  testRequestWithBody("");

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, ResponseEmptyPayloadValue) {
  initializeFilter(config_yaml_);

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));

  testResponseWithBody("");

  EXPECT_EQ(getCounterValue("json_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.no_body"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.invalid_json_body"), 0);
}

TEST_F(FilterTest, InvalidJsonPayload) {
  initializeFilter(config_yaml_);
  // missing right-most curly brace
  const std::string request_body =
      R"delimiter(
      {"version":"1.0.0",
      "messages":[
        {"role":"user","content":"content A"},
        {"role":"assistant","content":"content B"},
        {"role":"user","content":"content C"},
        {"role":"assistant","content":"content D"},
        {"role":"user","content":"content E"}],
      "stream":true)delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "error")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 1);
}

TEST_F(FilterTest, OnMissingQuotedString) {
  initializeFilter(config_yaml_);
  const std::string request_body = R"delimiter("")delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "unknown")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, OnMissingQuotedJsonObject) {
  initializeFilter(config_yaml_);
  const std::string request_body =
      R"delimiter("{\"model\": \"gpt-3.5-turbo\",\"temperature\": 0.2,\"stream\": false}")delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "unknown")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, OnMissingPureNumber) {
  initializeFilter(config_yaml_);
  const std::string request_body = R"delimiter(5566)delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "unknown")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

// TODO(kuochunghsu): Planned to support trimming.
TEST_F(FilterTest, InvalidJsonForAdditionalPrefixSuffix) {
  initializeFilter(config_yaml_);
  // missing right-most curly brace
  const std::string request_body =
      R"delimiter(data: {"id":"ID","object":"chat.completion.chunk","created":1686100940,"version":"1.0.0-0301"}\n\ndata: [DONE]\n\n)delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "error")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 1);
}

TEST_F(FilterTest, EmptyStringValue) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
)EOF");

  const std::string request_body = R"delimiter({"version":""})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));

  // No metadata is set from stream info.
  EXPECT_CALL(stream_info_, setDynamicMetadata(_, _)).Times(0);
  testRequestWithBody(request_body);

  Buffer::OwnedImpl buffer(request_body);
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, PayloadValueTooLong) {
  initializeFilter(R"EOF(
request_rules:
  rules:
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

  const std::string value(MAX_PAYLOAD_VALUE_LEN + 1, 'z');
  const std::string request_body =
      absl::StrCat(R"delimiter({"version":")delimiter", value, R"delimiter("})delimiter");

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "unknown")))));
  Buffer::OwnedImpl buffer(request_body);
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, PayloadValueTooLongValueTypeString) {
  initializeFilter(R"EOF(
request_rules:
  rules:
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

  const std::string value(MAX_PAYLOAD_VALUE_LEN + 1, 'z');
  const std::string request_body =
      absl::StrCat(R"delimiter({"version":")delimiter", value, R"delimiter("})delimiter");

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "unknown")))));
  Buffer::OwnedImpl buffer(request_body);
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, MissingMetadataKeyAndFallbackValue) {
  initializeFilter(config_yaml_);
  const std::string request_body =
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
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "unknown")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, MissingMetadataKeyWithNoFallbackValue) {
  const std::string yaml = R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
)EOF";

  initializeFilter(yaml);
  const std::string request_body =
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

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, MissingMetadataKeyWithExistingMetadata) {
  initializeFilter(config_yaml_);
  const std::string request_body =
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

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, MultipleRules) {
  initializeFilter(R"EOF(
request_rules:
  rules:
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
  const std::string request_body =
      R"delimiter({"version":"good version", "id":"beautiful id"})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "good version"),
                                                 IsStructString("id", "beautiful id")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, MultipleRulesInSamePath) {
  initializeFilter(R"EOF(
request_rules:
  rules:
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
  const std::string request_body =
      R"delimiter({"version":"good version", "id":"beautiful id"})delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "good version")))));
  EXPECT_CALL(stream_info_, setDynamicMetadata("another.namespace",
                                               HasStructFields(UnorderedElementsAre(
                                                   IsStructString("version", "good version")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, MultipleRulesSecondLayer) {
  initializeFilter(R"EOF(
request_rules:
  rules:
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
  const std::string request_body = R"delimiter({"version":"good version", "messages":{
      "foo":"bar",
      "baz":"qux"
    }
  })delimiter";

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb",
                                 HasStructFields(UnorderedElementsAre(
                                     IsStructString("version", "good version"),
                                     IsStructString("foo", "bar"), IsStructString("baz", "qux")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, CustomRequestAllowContentTypeAccepted) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
  allow_content_types:
  - "application/better-json"
)EOF");
  const std::string request_body = R"delimiter({"version":"good version"})delimiter";

  Http::TestRequestHeaderMapImpl matched_incoming_headers{
      {":path", "/ping"}, {":method", "GET"}, {"Content-Type", "application/better-json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(matched_incoming_headers, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "good version")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, CustomResponseAllowContentTypeAccepted) {
  initializeFilter(R"EOF(
response_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
  allow_content_types:
  - "application/better-json"
)EOF");
  const std::string response_body = R"delimiter({"version":"good version"})delimiter";

  Http::TestResponseHeaderMapImpl matched_incoming_headers{
      {":path", "/ping"}, {"Content-Type", "application/better-json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(matched_incoming_headers, false));

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "good version")))));
  testResponseWithBody(response_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.resp.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.invalid_json_body"), 0);
}

TEST_F(FilterTest, CustomRequestAllowContentTypeRejected) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_error:
      metadata_namespace: envoy.lb
      key: version
      value: 'error'
  allow_content_types:
  - "application/non-json"
)EOF");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "error")))));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(incoming_headers_, false));

  testRequestWithBody("{}");

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, CustomResponseAllowContentTypeRejected) {
  initializeFilter(R"EOF(
response_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_error:
      metadata_namespace: envoy.lb
      key: version
      value: 'error'
  allow_content_types:
  - "application/non-json"
)EOF");
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "error")))));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  testResponseWithBody("{}");

  EXPECT_EQ(getCounterValue("json_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.mismatched_content_type"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.invalid_json_body"), 0);
}

TEST_F(FilterTest, RequestAllowEmptyContentType) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
  allow_empty_content_type: true
)EOF");
  const std::string request_body = R"delimiter({"version":"good version"})delimiter";

  Http::TestRequestHeaderMapImpl matched_incoming_headers{{":path", "/ping"}, {":method", "GET"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(matched_incoming_headers, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "good version")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, ResponseAllowEmptyContentType) {
  initializeFilter(R"EOF(
response_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
  allow_empty_content_type: true
)EOF");
  const std::string response_body = R"delimiter({"version":"good version"})delimiter";

  Http::TestResponseHeaderMapImpl matched_incoming_headers{{":path", "/ping"}, {":method", "GET"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(matched_incoming_headers, false));

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "good version")))));
  testResponseWithBody(response_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.resp.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.invalid_json_body"), 0);
}

TEST_F(FilterTest, RequestBodyWithResponseRule) {
  initializeFilter(R"EOF(
response_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
)EOF");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(incoming_headers_, false));

  const std::string request_body = R"delimiter({"version":"good version"})delimiter";

  testRequestWithBody(request_body);

  Http::TestRequestTrailerMapImpl trailers{{"some", "trailer"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, ResponseBodyWithRequestRule) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
)EOF");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));

  const std::string response_body = R"delimiter({"version":"good version"})delimiter";

  testResponseWithBody(response_body);

  Http::TestResponseTrailerMapImpl trailers{{"some", "trailer"}};
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));

  EXPECT_EQ(getCounterValue("json_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.invalid_json_body"), 0);
}

TEST_F(FilterTest, RequestAllowContentTypeRegex) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
  allow_content_types_regex:
    google_re2: {}
    regex: "application/.*"
)EOF");
  const std::string request_body = R"delimiter({"version":"good version"})delimiter";

  Http::TestRequestHeaderMapImpl matched_incoming_headers{
      {":path", "/ping"}, {":method", "GET"}, {"Content-Type", "application/better-json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(matched_incoming_headers, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "good version")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, RequestAllowContentTypemultipleRegex) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
  allow_content_types_regex:
    google_re2: {}
    regex: "(?:text|application)/.*"
)EOF");
  const std::string request_body = R"delimiter({"version":"good version"})delimiter";

  Http::TestRequestHeaderMapImpl matched_incoming_headers{
      {":path", "/ping"}, {":method", "GET"}, {"Content-Type", "application/better-json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(matched_incoming_headers, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "good version")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, RequestAllowContentTypeandRegex) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_missing:
      metadata_namespace: envoy.lb
      key: version
      value: 'unknown'
  allow_content_types:
  - application/better-json
  allow_content_types_regex:
    google_re2: {}
    regex: "application/.*"
)EOF");
  const std::string request_body = R"delimiter({"version":"good version"})delimiter";

  Http::TestRequestHeaderMapImpl matched_incoming_headers{
      {":path", "/ping"}, {":method", "GET"}, {"Content-Type", "application/better-json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(matched_incoming_headers, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "good version")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, RequestAllowContentTypeRegexMatch) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_missing:
      metadata_namespace: envoy.lb
      key: version
      value: 'unknown'
  allow_content_types:
  - application/better-json
  allow_content_types_regex:
    google_re2: {}
    regex: "application/.*"
)EOF");
  const std::string request_body_json = R"delimiter({"version":"good version"})delimiter";

  Http::TestRequestHeaderMapImpl matched_incoming_headers_json{
      {":path", "/ping"}, {":method", "GET"}, {"Content-Type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(matched_incoming_headers_json, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "good version")))));
  testRequestWithBody(request_body_json);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, CustomRequestAllowContentTypeNoMatch) {
  initializeFilter(R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_error:
      metadata_namespace: envoy.lb
      key: version
      value: 'error'
  allow_content_types:
  - "text/plain"
  allow_content_types_regex:
    google_re2: {}
    regex: "application/.*"
)EOF");
  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "error")))));

  Http::TestRequestHeaderMapImpl nomatch_incoming_headers{
      {":path", "/ping"}, {":method", "POST"}, {"Content-Type", "image/png"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->decodeHeaders(nomatch_incoming_headers, false));

  testRequestWithBody("{}");

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.mismatched_content_type"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.rq.invalid_json_body"), 0);
}

TEST_F(FilterTest, CustomResponseAllowContentTypeNoMatch) {
  initializeFilter(R"EOF(
response_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_error:
      metadata_namespace: envoy.lb
      key: version
      value: 'error'
  allow_content_types:
  - "text/plain"
  allow_content_types_regex:
    google_re2: {}
    regex: "application/.*"
)EOF");
  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "error")))));

  Http::TestResponseHeaderMapImpl nomatch_incoming_headers{{":path", "/ping"}, {":method", "GET"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encodeHeaders(nomatch_incoming_headers, false));

  testResponseWithBody("{}");

  EXPECT_EQ(getCounterValue("json_to_metadata.resp.success"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.mismatched_content_type"), 1);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.no_body"), 0);
  EXPECT_EQ(getCounterValue("json_to_metadata.resp.invalid_json_body"), 0);
}

// Test per-route override functionality
TEST_F(FilterTest, PerRouteOverride) {
  // Global config is empty (no rules)
  initializeFilter("{}");

  const std::string request_body = R"delimiter({"version":"2.0.0"})delimiter";

  // Setup per route config
  const std::string per_route_config_yaml = R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
)EOF";

  std::shared_ptr<FilterConfig> per_route_config = createConfig(per_route_config_yaml, true);
  EXPECT_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillOnce(Return(per_route_config.get()));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "2.0.0")))));
  testRequestWithBody(request_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.rq.success"), 1);
}

// Test that per-route config is cached
TEST_F(FilterTest, PerRouteConfigIsCached) {
  // Global config is empty
  const std::string empty_config = R"EOF(
request_rules:
  rules: []
response_rules:
  rules: []
)EOF";

  initializeFilter(empty_config);

  // Setup per route config
  const std::string per_route_config_yaml = R"EOF(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
)EOF";

  std::shared_ptr<FilterConfig> per_route_config = createConfig(per_route_config_yaml, true);

  // mostSpecificPerFilterConfig should only be called once due to caching
  EXPECT_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillOnce(Return(per_route_config.get()));

  // First call - fetches and caches the config
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(incoming_headers_, false));

  // Subsequent operations should use cached config without additional lookups
  const std::string request_body = R"delimiter({"version":"2.0.0"})delimiter";

  EXPECT_CALL(decoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "2.0.0")))));
  testRequestWithBody(request_body);
}

// Test per-route config with response rules
TEST_F(FilterTest, PerRouteOverrideResponse) {
  // Global config is empty
  initializeFilter("{}");

  const std::string response_body = R"delimiter({"version":"3.0.0"})delimiter";

  // Setup per route config with response rules
  const std::string per_route_config_yaml = R"EOF(
response_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
)EOF";

  std::shared_ptr<FilterConfig> per_route_config = createConfig(per_route_config_yaml, true);
  EXPECT_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillOnce(Return(per_route_config.get()));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "3.0.0")))));
  testResponseWithBody(response_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.resp.success"), 1);
}

// Test that a route-level response rule is used when the global config only has a request rule.
TEST_F(FilterTest, PerRouteOverridesGlobalConfig) {
  // Global config has a request rule.
  const std::string global_config = R"EOF(
request_rules:
  rules:
  - selectors:
    - key: old_key
    on_present:
      metadata_namespace: envoy.lb
      key: old_value
)EOF";

  initializeFilter(global_config);

  // Per-route config has a response rule.
  const std::string per_route_config_yaml = R"EOF(
response_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
)EOF";

  const std::string response_body = R"delimiter({"version":"3.0.0"})delimiter";

  std::shared_ptr<FilterConfig> per_route_config = createConfig(per_route_config_yaml, true);
  EXPECT_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillOnce(Return(per_route_config.get()));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));

  EXPECT_CALL(encoder_callbacks_, streamInfo()).WillRepeatedly(ReturnRef(stream_info_));
  EXPECT_CALL(stream_info_,
              setDynamicMetadata("envoy.lb", HasStructFields(UnorderedElementsAre(
                                                 IsStructString("version", "3.0.0")))));
  testResponseWithBody(response_body);

  EXPECT_EQ(getCounterValue("json_to_metadata.resp.success"), 1);
}

} // namespace JsonToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
