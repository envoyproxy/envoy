#include <chrono>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/access_loggers/open_telemetry/grpc_access_log_impl.h"
#include "source/extensions/access_loggers/open_telemetry/substitution_formatter.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "opentelemetry/proto/common/v1/common.pb.h"

using ::opentelemetry::proto::common::v1::KeyValueList;
using OpenTelemetryFormatMap = std::list<std::pair<std::string, std::string>>;
using testing::Const;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {
namespace {

class TestSerializedStructFilterState : public StreamInfo::FilterState::Object {
public:
  TestSerializedStructFilterState() : use_struct_(true) {
    (*struct_.mutable_fields())["inner_key"] = ValueUtil::stringValue("inner_value");
  }

  explicit TestSerializedStructFilterState(const ProtobufWkt::Struct& s) : use_struct_(true) {
    struct_.CopyFrom(s);
  }

  explicit TestSerializedStructFilterState(std::chrono::seconds seconds) {
    duration_.set_seconds(seconds.count());
  }

  ProtobufTypes::MessagePtr serializeAsProto() const override {
    if (use_struct_) {
      auto s = std::make_unique<ProtobufWkt::Struct>();
      s->CopyFrom(struct_);
      return s;
    }

    auto d = std::make_unique<ProtobufWkt::Duration>();
    d->CopyFrom(duration_);
    return d;
  }

private:
  const bool use_struct_{false};
  ProtobufWkt::Struct struct_;
  ProtobufWkt::Duration duration_;
};

// Class used to test serializeAsString and serializeAsProto of FilterState
class TestSerializedStringFilterState : public StreamInfo::FilterState::Object {
public:
  TestSerializedStringFilterState(std::string str) : raw_string_(str) {}
  absl::optional<std::string> serializeAsString() const override {
    return raw_string_ + " By PLAIN";
  }
  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto message = std::make_unique<ProtobufWkt::StringValue>();
    message->set_value(raw_string_ + " By TYPED");
    return message;
  }

private:
  std::string raw_string_;
};

/**
 * Populate a metadata object with the following test data:
 * "com.test": {"test_key":"test_value","test_obj":{"inner_key":"inner_value"}}
 */
void populateMetadataTestData(envoy::config::core::v3::Metadata& metadata) {
  ProtobufWkt::Struct struct_obj;
  auto& fields_map = *struct_obj.mutable_fields();
  fields_map["test_key"] = ValueUtil::stringValue("test_value");
  ProtobufWkt::Struct struct_inner;
  (*struct_inner.mutable_fields())["inner_key"] = ValueUtil::stringValue("inner_value");
  ProtobufWkt::Value val;
  *val.mutable_struct_value() = struct_inner;
  fields_map["test_obj"] = val;
  (*metadata.mutable_filter_metadata())["com.test"] = struct_obj;
}

void verifyOpenTelemetryOutput(KeyValueList output, OpenTelemetryFormatMap expected_map) {
  EXPECT_EQ(output.values().size(), expected_map.size());
  OpenTelemetryFormatMap list_output;
  for (const auto& pair : output.values()) {
    list_output.emplace_back(pair.key(), pair.value().string_value());
  }

  for (auto output_it = list_output.begin(), expected_it = expected_map.begin();
       output_it != list_output.end() && expected_it != expected_map.end();
       ++output_it, ++expected_it) {
    EXPECT_EQ(*output_it, *expected_it);
  }
}

TEST(SubstitutionFormatterTest, OpenTelemetryFormatterPlainStringTest) {
  StreamInfo::MockStreamInfo stream_info;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  OpenTelemetryFormatMap expected = {{"plain_string", "plain_string_value"}};

  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "plain_string"
        value:
          string_value: "plain_string_value"
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});

  verifyOpenTelemetryOutput(formatter.format({}, stream_info), expected);
}

TEST(SubstitutionFormatterTest, OpenTelemetryFormatterTypesTest) {
  StreamInfo::MockStreamInfo stream_info;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "string_type"
        value:
          string_value: "plain_string_value"
      - key: "kvlist_type"
        value:
          kvlist_value:
            values:
              - key: "plain_string"
                value:
                  string_value: "plain_string_value"
              - key: "protocol"
                value:
                  string_value: "%PROTOCOL%"
      - key: "array_type"
        value:
          array_value:
            values:
              - string_value: "plain_string_value"
              - string_value: "%PROTOCOL%"
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});

  KeyValueList expected;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "string_type"
        value:
          string_value: "plain_string_value"
      - key: "kvlist_type"
        value:
          kvlist_value:
            values:
              - key: "plain_string"
                value:
                  string_value: "plain_string_value"
              - key: "protocol"
                value:
                  string_value: "HTTP/1.1"
      - key: "array_type"
        value:
          array_value:
            values:
              - string_value: "plain_string_value"
              - string_value: "HTTP/1.1"
  )EOF",
                            expected);
  const KeyValueList output = formatter.format({}, stream_info);
  EXPECT_TRUE(TestUtility::protoEqual(output, expected));
}

// Test that nested values are formatted properly, including inter-type nesting.
TEST(SubstitutionFormatterTest, OpenTelemetryFormatterNestedObjectsTest) {
  StreamInfo::MockStreamInfo stream_info;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  KeyValueList key_mapping;
  // For both kvlist and array, we test 3 nesting levels of all types (string, kvlist and
  // array).
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "kvlist"
        value:
          kvlist_value:
            values:
              - key: "kvlist_string"
                value:
                  string_value: "plain_string_value"
              - key: "kvlist_protocol"
                value:
                  string_value: "%PROTOCOL%"
              - key: "kvlist_kvlist"
                value:
                  kvlist_value:
                    values:
                      - key: "kvlist_kvlist_string"
                        value:
                          string_value: "plain_string_value"
                      - key: "kvlist_kvlist_protocol"
                        value:
                          string_value: "%PROTOCOL%"
                      - key: "kvlist_kvlist_kvlist"
                        value:
                          kvlist_value:
                            values:
                              - key: "kvlist_kvlist_kvlist_string"
                                value:
                                  string_value: "plain_string_value"
                              - key: "kvlist_kvlist_kvlist_protocol"
                                value:
                                  string_value: "%PROTOCOL%"
                      - key: "kvlist_kvlist_array"
                        value:
                          array_value:
                            values:
                              - string_value: "kvlist_kvlist_array_string"
                              - string_value: "%PROTOCOL%"
              - key: "kvlist_array"
                value:
                  array_value:
                    values:
                      - string_value: "kvlist_array_string"
                      - string_value: "%PROTOCOL%"
                      - kvlist_value:
                          values:
                            - key: "kvlist_array_kvlist_string"
                              value:
                                string_value: "plain_string_value"
                            - key: "kvlist_array_kvlist_protocol"
                              value:
                                string_value: "%PROTOCOL%"
                      - array_value:
                          values:
                            - string_value: "kvlist_array_array_string"
                            - string_value: "%PROTOCOL%"
      - key: "array"
        value:
          array_value:
            values:
              - string_value: "array_string"
              - string_value: "%PROTOCOL%"
              - kvlist_value:
                  values:
                    - key: "array_kvlist_string"
                      value:
                        string_value: "plain_string_value"
                    - key: "array_kvlist_protocol"
                      value:
                        string_value: "%PROTOCOL%"
                    - key: "array_kvlist_kvlist"
                      value:
                        kvlist_value:
                          values:
                            - key: "array_kvlist_kvlist_string"
                              value:
                                string_value: "plain_string_value"
                            - key: "array_kvlist_kvlist_protocol"
                              value:
                                string_value: "%PROTOCOL%"
                    - key: "array_kvlist_array"
                      value:
                        array_value:
                          values:
                            - string_value: "array_kvlist_array_string"
                            - string_value: "%PROTOCOL%"
              - array_value:
                  values:
                    - string_value: "array_array_string"
                    - string_value: "%PROTOCOL%"
                    - array_value:
                        values:
                          - string_value: "array_array_array_string"
                          - string_value: "%PROTOCOL%"
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});
  KeyValueList expected;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "kvlist"
        value:
          kvlist_value:
            values:
              - key: "kvlist_string"
                value:
                  string_value: "plain_string_value"
              - key: "kvlist_protocol"
                value:
                  string_value: "HTTP/1.1"
              - key: "kvlist_kvlist"
                value:
                  kvlist_value:
                    values:
                      - key: "kvlist_kvlist_string"
                        value:
                          string_value: "plain_string_value"
                      - key: "kvlist_kvlist_protocol"
                        value:
                          string_value: "HTTP/1.1"
                      - key: "kvlist_kvlist_kvlist"
                        value:
                          kvlist_value:
                            values:
                              - key: "kvlist_kvlist_kvlist_string"
                                value:
                                  string_value: "plain_string_value"
                              - key: "kvlist_kvlist_kvlist_protocol"
                                value:
                                  string_value: "HTTP/1.1"
                      - key: "kvlist_kvlist_array"
                        value:
                          array_value:
                            values:
                              - string_value: "kvlist_kvlist_array_string"
                              - string_value: "HTTP/1.1"
              - key: "kvlist_array"
                value:
                  array_value:
                    values:
                      - string_value: "kvlist_array_string"
                      - string_value: "HTTP/1.1"
                      - kvlist_value:
                          values:
                            - key: "kvlist_array_kvlist_string"
                              value:
                                string_value: "plain_string_value"
                            - key: "kvlist_array_kvlist_protocol"
                              value:
                                string_value: "HTTP/1.1"
                      - array_value:
                          values:
                            - string_value: "kvlist_array_array_string"
                            - string_value: "HTTP/1.1"
      - key: "array"
        value:
          array_value:
            values:
              - string_value: "array_string"
              - string_value: "HTTP/1.1"
              - kvlist_value:
                  values:
                    - key: "array_kvlist_string"
                      value:
                        string_value: "plain_string_value"
                    - key: "array_kvlist_protocol"
                      value:
                        string_value: "HTTP/1.1"
                    - key: "array_kvlist_kvlist"
                      value:
                        kvlist_value:
                          values:
                            - key: "array_kvlist_kvlist_string"
                              value:
                                string_value: "plain_string_value"
                            - key: "array_kvlist_kvlist_protocol"
                              value:
                                string_value: "HTTP/1.1"
                    - key: "array_kvlist_array"
                      value:
                        array_value:
                          values:
                            - string_value: "array_kvlist_array_string"
                            - string_value: "HTTP/1.1"
              - array_value:
                  values:
                    - string_value: "array_array_string"
                    - string_value: "HTTP/1.1"
                    - array_value:
                        values:
                          - string_value: "array_array_array_string"
                          - string_value: "HTTP/1.1"
  )EOF",
                            expected);
  const KeyValueList output = formatter.format({}, stream_info);
  EXPECT_TRUE(TestUtility::protoEqual(output, expected));
}

TEST(SubstitutionFormatterTest, OpenTelemetryFormatterSingleOperatorTest) {
  StreamInfo::MockStreamInfo stream_info;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  OpenTelemetryFormatMap expected = {{"protocol", "HTTP/1.1"}};

  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "protocol"
        value:
          string_value: "%PROTOCOL%"
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});

  verifyOpenTelemetryOutput(formatter.format({}, stream_info), expected);
}

TEST(SubstitutionFormatterTest, EmptyOpenTelemetryFormatterTest) {
  StreamInfo::MockStreamInfo stream_info;

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  OpenTelemetryFormatMap expected = {{"protocol", ""}};

  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "protocol"
        value:
          string_value: ""
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});

  verifyOpenTelemetryOutput(formatter.format({}, stream_info), expected);
}

TEST(SubstitutionFormatterTest, OpenTelemetryFormatterNonExistentHeaderTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"some_request_header", "SOME_REQUEST_HEADER"}};
  Http::TestResponseHeaderMapImpl response_header{{"some_response_header", "SOME_RESPONSE_HEADER"}};

  OpenTelemetryFormatMap expected = {{"protocol", "HTTP/1.1"},
                                     {"some_request_header", "SOME_REQUEST_HEADER"},
                                     {"nonexistent_response_header", "-"},
                                     {"some_response_header", "SOME_RESPONSE_HEADER"}};

  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    values:
    - key: "protocol"
      value:
        string_value: "%PROTOCOL%"
    - key: "some_request_header"
      value:
        string_value: "%REQ(some_request_header)%"
    - key: "nonexistent_response_header"
      value:
        string_value: "%RESP(nonexistent_response_header)%"
    - key: "some_response_header"
      value:
        string_value: "%RESP(some_response_header)%"
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  verifyOpenTelemetryOutput(formatter.format({&request_header, &response_header}, stream_info),
                            expected);
}

TEST(SubstitutionFormatterTest, OpenTelemetryFormatterAlternateHeaderTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{
      {"request_present_header", "REQUEST_PRESENT_HEADER"}};
  Http::TestResponseHeaderMapImpl response_header{
      {"response_present_header", "RESPONSE_PRESENT_HEADER"}};

  OpenTelemetryFormatMap expected = {
      {"request_present_header_or_request_absent_header", "REQUEST_PRESENT_HEADER"},
      {"request_absent_header_or_request_present_header", "REQUEST_PRESENT_HEADER"},
      {"response_absent_header_or_response_absent_header", "RESPONSE_PRESENT_HEADER"},
      {"response_present_header_or_response_absent_header", "RESPONSE_PRESENT_HEADER"}};

  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "request_present_header_or_request_absent_header"
        value:
          string_value: "%REQ(request_present_header?request_absent_header)%"
      - key: "request_absent_header_or_request_present_header"
        value:
          string_value: "%REQ(request_absent_header?request_present_header)%"
      - key: "response_absent_header_or_response_absent_header"
        value:
          string_value: "%RESP(response_absent_header?response_present_header)%"
      - key: "response_present_header_or_response_absent_header"
        value:
          string_value: "%RESP(response_present_header?response_absent_header)%"
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});

  absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
  EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

  verifyOpenTelemetryOutput(formatter.format({&request_header, &response_header}, stream_info),
                            expected);
}

TEST(SubstitutionFormatterTest, OpenTelemetryFormatterDynamicMetadataTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  EXPECT_CALL(stream_info, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));
  EXPECT_CALL(Const(stream_info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));

  OpenTelemetryFormatMap expected = {{"test_key", "test_value"},
                                     {"test_obj", "{\"inner_key\":\"inner_value\"}"},
                                     {"test_obj.inner_key", "inner_value"}};

  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "test_key"
        value:
          string_value: "%DYNAMIC_METADATA(com.test:test_key)%"
      - key: "test_obj"
        value:
          string_value: "%DYNAMIC_METADATA(com.test:test_obj)%"
      - key: "test_obj.inner_key"
        value:
          string_value: "%DYNAMIC_METADATA(com.test:test_obj:inner_key)%"
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});

  verifyOpenTelemetryOutput(
      formatter.format({&request_header, &response_header, &response_trailer}, stream_info),
      expected);
}

TEST(SubstitutionFormatterTest, OpenTelemetryFormatterClusterMetadataTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};

  envoy::config::core::v3::Metadata metadata;
  populateMetadataTestData(metadata);
  absl::optional<std::shared_ptr<NiceMock<Upstream::MockClusterInfo>>> cluster =
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>();
  EXPECT_CALL(**cluster, metadata()).WillRepeatedly(ReturnRef(metadata));
  EXPECT_CALL(stream_info, upstreamClusterInfo()).WillRepeatedly(ReturnPointee(cluster));
  EXPECT_CALL(Const(stream_info), upstreamClusterInfo()).WillRepeatedly(ReturnPointee(cluster));

  OpenTelemetryFormatMap expected = {
      {"test_key", "test_value"},
      {"test_obj", "{\"inner_key\":\"inner_value\"}"},
      {"test_obj.inner_key", "inner_value"},
      {"test_obj.non_existing_key", "-"},
  };

  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "test_key"
        value:
          string_value: "%CLUSTER_METADATA(com.test:test_key)%"
      - key: "test_obj"
        value:
            string_value: "%CLUSTER_METADATA(com.test:test_obj)%"
      - key: "test_obj.inner_key"
        value:
            string_value: "%CLUSTER_METADATA(com.test:test_obj:inner_key)%"
      - key: "test_obj.non_existing_key"
        value:
            string_value: "%CLUSTER_METADATA(com.test:test_obj:non_existing_key)%"
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});

  verifyOpenTelemetryOutput(
      formatter.format({&request_header, &response_header, &response_trailer}, stream_info),
      expected);
}

TEST(SubstitutionFormatterTest, OpenTelemetryFormatterClusterMetadataNoClusterInfoTest) {
  StreamInfo::MockStreamInfo stream_info;
  Http::TestRequestHeaderMapImpl request_header{{"first", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_header{{"second", "PUT"}, {"test", "test"}};
  Http::TestResponseTrailerMapImpl response_trailer{{"third", "POST"}, {"test-2", "test-2"}};

  OpenTelemetryFormatMap expected = {{"test_key", "-"}};

  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "test_key"
        value:
          string_value: "%CLUSTER_METADATA(com.test:test_key)%"
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});

  // Empty optional (absl::nullopt)
  {
    EXPECT_CALL(Const(stream_info), upstreamClusterInfo()).WillOnce(Return(absl::nullopt));
    verifyOpenTelemetryOutput(
        formatter.format({&request_header, &response_header, &response_trailer}, stream_info),
        expected);
  }
  // Empty cluster info (nullptr)
  {
    EXPECT_CALL(Const(stream_info), upstreamClusterInfo()).WillOnce(Return(nullptr));
    verifyOpenTelemetryOutput(
        formatter.format({&request_header, &response_header, &response_trailer}, stream_info),
        expected);
  }
}

TEST(SubstitutionFormatterTest, OpenTelemetryFormatterFilterStateTest) {
  StreamInfo::MockStreamInfo stream_info;
  stream_info.filter_state_->setData("test_key",
                                     std::make_unique<Router::StringAccessorImpl>("test_value"),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  stream_info.filter_state_->setData("test_obj",
                                     std::make_unique<TestSerializedStructFilterState>(),
                                     StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));

  OpenTelemetryFormatMap expected = {{"test_key", "\"test_value\""},
                                     {"test_obj", "{\"inner_key\":\"inner_value\"}"}};

  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
  values:
    - key: "test_key"
      value:
        string_value: "%FILTER_STATE(test_key)%"
    - key: "test_obj"
      value:
        string_value: "%FILTER_STATE(test_obj)%"
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});

  verifyOpenTelemetryOutput(formatter.format({}, stream_info), expected);
}

TEST(SubstitutionFormatterTest, OpenTelemetryFormatterUpstreamFilterStateTest) {

  StreamInfo::MockStreamInfo stream_info;

  const StreamInfo::FilterStateSharedPtr upstream_filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Request);
  upstream_filter_state->setData("test_key",
                                 std::make_unique<Router::StringAccessorImpl>("test_value"),
                                 StreamInfo::FilterState::StateType::ReadOnly);
  upstream_filter_state->setData("test_obj", std::make_unique<TestSerializedStructFilterState>(),
                                 StreamInfo::FilterState::StateType::ReadOnly);

  EXPECT_CALL(stream_info, upstreamInfo()).Times(testing::AtLeast(1));
  // Get pointer to MockUpstreamInfo.
  std::shared_ptr<StreamInfo::MockUpstreamInfo> mock_upstream_info =
      std::dynamic_pointer_cast<StreamInfo::MockUpstreamInfo>(stream_info.upstreamInfo());
  EXPECT_CALL(Const(stream_info), upstreamInfo()).Times(testing::AtLeast(1));
  EXPECT_CALL(Const(*mock_upstream_info), upstreamFilterState())
      .Times(2)
      .WillRepeatedly(ReturnRef(upstream_filter_state));

  OpenTelemetryFormatMap expected = {{"test_key", "\"test_value\""},
                                     {"test_obj", "{\"inner_key\":\"inner_value\"}"}};

  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
  values:
    - key: "test_key"
      value:
        string_value: "%UPSTREAM_FILTER_STATE(test_key)%"
    - key: "test_obj"
      value:
        string_value: "%UPSTREAM_FILTER_STATE(test_obj)%"
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});

  verifyOpenTelemetryOutput(formatter.format({}, stream_info), expected);
}

// Test new specifier (PLAIN/TYPED) of FilterState. Ensure that after adding additional specifier,
// the FilterState can call the serializeAsProto or serializeAsString methods correctly.
TEST(SubstitutionFormatterTest, OpenTelemetryFormatterFilterStateSpeciferTest) {
  StreamInfo::MockStreamInfo stream_info;
  stream_info.filter_state_->setData(
      "test_key", std::make_unique<TestSerializedStringFilterState>("test_value"),
      StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_CALL(Const(stream_info), filterState()).Times(testing::AtLeast(1));

  OpenTelemetryFormatMap expected = {
      {"test_key_plain", "test_value By PLAIN"},
      {"test_key_typed", "\"test_value By TYPED\""},
  };

  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "test_key_plain"
        value:
          string_value: "%FILTER_STATE(test_key:PLAIN)%"
      - key: "test_key_typed"
        value:
          string_value: "%FILTER_STATE(test_key:TYPED)%"
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});

  verifyOpenTelemetryOutput(formatter.format({}, stream_info), expected);
}

// Test new specifier (PLAIN/TYPED) of FilterState. Ensure that after adding additional specifier,
// the FilterState can call the serializeAsProto or serializeAsString methods correctly.
TEST(SubstitutionFormatterTest, OpenTelemetryFormatterUpstreamFilterStateSpeciferTest) {
  StreamInfo::MockStreamInfo stream_info;

  stream_info.upstream_info_ = std::make_shared<StreamInfo::UpstreamInfoImpl>();
  stream_info.upstream_info_->setUpstreamFilterState(
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Request));

  stream_info.upstream_info_->upstreamFilterState()->setData(
      "test_key", std::make_unique<TestSerializedStringFilterState>("test_value"),
      StreamInfo::FilterState::StateType::ReadOnly);

  EXPECT_CALL(Const(stream_info), upstreamInfo()).Times(testing::AtLeast(1));

  OpenTelemetryFormatMap expected = {
      {"test_key_plain", "test_value By PLAIN"},
      {"test_key_typed", "\"test_value By TYPED\""},
  };

  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "test_key_plain"
        value:
          string_value: "%UPSTREAM_FILTER_STATE(test_key:PLAIN)%"
      - key: "test_key_typed"
        value:
          string_value: "%UPSTREAM_FILTER_STATE(test_key:TYPED)%"
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});

  verifyOpenTelemetryOutput(formatter.format({}, stream_info), expected);
}

// Error specifier will cause an exception to be thrown.
TEST(SubstitutionFormatterTest, OpenTelemetryFormatterFilterStateErrorSpeciferTest) {
  StreamInfo::MockStreamInfo stream_info;
  std::string body;
  stream_info.filter_state_->setData(
      "test_key", std::make_unique<TestSerializedStringFilterState>("test_value"),
      StreamInfo::FilterState::StateType::ReadOnly);

  // 'ABCDE' is error specifier.
  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "test_key_plain"
        value:
          string_value: "%FILTER_STATE(test_key:ABCDE)%"
      - key: "test_key_typed"
        value:
          string_value: "%FILTER_STATE(test_key:TYPED)%"
  )EOF",
                            key_mapping);
  EXPECT_THROW_WITH_MESSAGE(OpenTelemetryFormatter formatter(key_mapping, {}), EnvoyException,
                            "Invalid filter state serialize type, only support PLAIN/TYPED/FIELD.");
}

TEST(SubstitutionFormatterTest, OpenTelemetryFormatterUpstreamFilterStateErrorSpeciferTest) {
  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  Http::TestResponseTrailerMapImpl response_trailers;
  StreamInfo::MockStreamInfo stream_info;
  std::string body;

  stream_info.upstream_info_ = std::make_shared<StreamInfo::UpstreamInfoImpl>();
  stream_info.upstream_info_->setUpstreamFilterState(
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Request));

  stream_info.upstream_info_->upstreamFilterState()->setData(
      "test_key", std::make_unique<TestSerializedStringFilterState>("test_value"),
      StreamInfo::FilterState::StateType::ReadOnly);

  // 'ABCDE' is error specifier.
  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "test_key_plain"
        value:
          string_value: "%UPSTREAM_FILTER_STATE(test_key:ABCDE)%"
      - key: "test_key_typed"
        value:
          string_value: "%UPSTREAM_FILTER_STATE(test_key:TYPED)%"
  )EOF",
                            key_mapping);
  EXPECT_THROW_WITH_MESSAGE(OpenTelemetryFormatter formatter(key_mapping, {}), EnvoyException,
                            "Invalid filter state serialize type, only support PLAIN/TYPED/FIELD.");
}

TEST(SubstitutionFormatterTest, OpenTelemetryFormatterStartTimeTest) {
  StreamInfo::MockStreamInfo stream_info;

  time_t expected_time_in_epoch = 1522280158;
  SystemTime time = std::chrono::system_clock::from_time_t(expected_time_in_epoch);
  EXPECT_CALL(stream_info, startTime()).WillRepeatedly(Return(time));

  OpenTelemetryFormatMap expected = {{"simple_date", "2018/03/28"},
                                     {"test_time", fmt::format("{}", expected_time_in_epoch)},
                                     {"bad_format", "bad_format"},
                                     {"default", "2018-03-28T23:35:58.000Z"},
                                     {"all_zeroes", "000000000.0.00.000"}};

  KeyValueList key_mapping;
  TestUtility::loadFromYaml(R"EOF(
    values:
      - key: "simple_date"
        value:
          string_value: "%START_TIME(%Y/%m/%d)%"
      - key: "test_time"
        value:
          string_value: "%START_TIME(%s)%"
      - key: "bad_format"
        value:
          string_value: "%START_TIME(bad_format)%"
      - key: "default"
        value:
          string_value: "%START_TIME%"
      - key: "all_zeroes"
        value:
          string_value: "%START_TIME(%f.%1f.%2f.%3f)%"
  )EOF",
                            key_mapping);
  OpenTelemetryFormatter formatter(key_mapping, {});

  verifyOpenTelemetryOutput(formatter.format({}, stream_info), expected);
}

TEST(SubstitutionFormatterTest, OpenTelemetryFormatterMultiTokenTest) {
  {
    StreamInfo::MockStreamInfo stream_info;
    Http::TestRequestHeaderMapImpl request_header{{"some_request_header", "SOME_REQUEST_HEADER"}};
    Http::TestResponseHeaderMapImpl response_header{
        {"some_response_header", "SOME_RESPONSE_HEADER"}};

    OpenTelemetryFormatMap expected = {
        {"multi_token_field", "HTTP/1.1 plainstring SOME_REQUEST_HEADER SOME_RESPONSE_HEADER"}};

    KeyValueList key_mapping;
    TestUtility::loadFromYaml(R"EOF(
      values:
        - key: "multi_token_field"
          value:
            string_value: "%PROTOCOL% plainstring %REQ(some_request_header)% %RESP(some_response_header)%"
    )EOF",
                              key_mapping);
    OpenTelemetryFormatter formatter(key_mapping, {});

    absl::optional<Http::Protocol> protocol = Http::Protocol::Http11;
    EXPECT_CALL(stream_info, protocol()).WillRepeatedly(Return(protocol));

    verifyOpenTelemetryOutput(formatter.format({&request_header, &response_header}, stream_info),
                              expected);
  }
}

#ifdef USE_CEL_PARSER
TEST(SubstitutionFormatterTest, CELFormatterTest) {
  {
    NiceMock<Server::Configuration::MockFactoryContext> context;
    StreamInfo::MockStreamInfo stream_info;
    Http::TestRequestHeaderMapImpl request_header{{"some_request_header", "SOME_REQUEST_HEADER"}};
    Http::TestResponseHeaderMapImpl response_header{
        {"some_response_header", "SOME_RESPONSE_HEADER"}};

    OpenTelemetryFormatMap expected = {{"cel_field", "SOME_REQUEST_HEADER SOME_RESPONSE_HEADER"}};

    envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig otel_config;
    TestUtility::loadFromYaml(R"EOF(
      resource_attributes:
        values:
          - key: "cel_field"
            value:
              string_value: "%CEL(request.headers['some_request_header'])% %CEL(response.headers['some_response_header'])%"
      formatters:
        - name: envoy.formatter.cel
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.formatter.cel.v3.Cel
    )EOF",
                              otel_config);
    auto commands = *Formatter::SubstitutionFormatStringUtils::parseFormatters(
        otel_config.formatters(), context);

    OpenTelemetryFormatter formatter(otel_config.resource_attributes(), commands);

    verifyOpenTelemetryOutput(formatter.format({&request_header, &response_header}, stream_info),
                              expected);
  }
}
#endif

} // namespace
} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
