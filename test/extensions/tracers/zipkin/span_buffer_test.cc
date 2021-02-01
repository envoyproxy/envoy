#include "envoy/config/trace/v3/zipkin.pb.h"

#include "common/network/utility.h"
#include "common/protobuf/utility.h"

#include "extensions/tracers/zipkin/span_buffer.h"
#include "extensions/tracers/zipkin/util.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

using testing::HasSubstr;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {
namespace {

// If this default timestamp is wrapped as double (using ValueUtil::numberValue()) and then it is
// serialized using Protobuf::util::MessageToJsonString, it renders as: 1.58432429547687e+15.
constexpr uint64_t DEFAULT_TEST_TIMESTAMP = 1584324295476870;
constexpr uint64_t DEFAULT_TEST_DURATION = 2584324295476870;
const Util::Replacements DEFAULT_TEST_REPLACEMENTS = {
    {"DEFAULT_TEST_TIMESTAMP", std::to_string(DEFAULT_TEST_TIMESTAMP)}};
const Util::Replacements DEFAULT_TEST_DURATIONS = {
    {"DEFAULT_TEST_DURATION", std::to_string(DEFAULT_TEST_DURATION)}};

enum class IpType { V4, V6 };

Endpoint createEndpoint(const IpType ip_type) {
  Endpoint endpoint;
  endpoint.setAddress(ip_type == IpType::V6
                          ? Envoy::Network::Utility::parseInternetAddress(
                                "2001:db8:85a3::8a2e:370:4444", 7334, true)
                          : Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 8080, false));
  endpoint.setServiceName("service1");
  return endpoint;
}

Annotation createAnnotation(const absl::string_view value, const IpType ip_type) {
  Annotation annotation;
  annotation.setValue(value.data());
  annotation.setTimestamp(DEFAULT_TEST_TIMESTAMP);
  annotation.setEndpoint(createEndpoint(ip_type));
  return annotation;
}

BinaryAnnotation createTag() {
  BinaryAnnotation tag;
  tag.setKey("response_size");
  // ensure duration replacement doesn't override this value.
  tag.setValue(std::to_string(DEFAULT_TEST_DURATION));
  return tag;
}

Span createSpan(const std::vector<absl::string_view>& annotation_values, const IpType ip_type) {
  Event::SimulatedTimeSystem simulated_time_system;
  Span span(simulated_time_system);
  span.setId(1);
  span.setTraceId(1);
  span.setDuration(DEFAULT_TEST_DURATION);
  std::vector<Annotation> annotations;
  annotations.reserve(annotation_values.size());
  for (absl::string_view value : annotation_values) {
    annotations.push_back(createAnnotation(value, ip_type));
  }
  span.setAnnotations(annotations);
  span.setBinaryAnnotations({createTag()});
  return span;
}

// To render a string with DEFAULT_TEST_TIMESTAMP and DEFAULT_TEST_DURATION placeholder with
// DEFAULT_TEST_TIMESTAMP and DEFAULT_TEST_DURATION values.
std::string withDefaultTimestampAndDuration(const std::string& expected) {
  const auto with_default_timestamp = absl::StrReplaceAll(expected, DEFAULT_TEST_REPLACEMENTS);
  return absl::StrReplaceAll(with_default_timestamp, DEFAULT_TEST_DURATIONS);
}

// To wrap JSON array string in a object for JSON string comparison through JsonStringEq test
// utility. Every DEFAULT_TEST_TIMESTAMP and DEFAULT_TEST_DURATION strings found in array_string
// will be replaced by DEFAULT_TEST_REPLACEMENTS and DEFAULT_TEST_DURATIONS respectively. i.e. to
// replace every DEFAULT_TEST_TIMESTAMP string occurrence with DEFAULT_TEST_TIMESTAMP value (the
// same with DEFAULT_TEST_DURATION).
std::string wrapAsObject(absl::string_view array_string) {
  return withDefaultTimestampAndDuration(absl::StrFormat(R"({"root":%s})", array_string));
}

void expectSerializedBuffer(SpanBuffer& buffer, const bool delay_allocation,
                            const std::vector<std::string>& expected_list) {
  Event::SimulatedTimeSystem test_time;

  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.serialize());

  if (delay_allocation) {
    EXPECT_FALSE(buffer.addSpan(createSpan({"cs", "sr"}, IpType::V4)));
    buffer.allocateBuffer(expected_list.size() + 1);
  }

  // Add span after allocation, but missing required annotations should be false.
  EXPECT_FALSE(buffer.addSpan(Span(test_time.timeSystem())));
  EXPECT_FALSE(buffer.addSpan(createSpan({"aa"}, IpType::V4)));

  for (uint64_t i = 0; i < expected_list.size(); i++) {
    buffer.addSpan(createSpan({"cs", "sr"}, IpType::V4));
    EXPECT_EQ(i + 1, buffer.pendingSpans());
    EXPECT_THAT(wrapAsObject(expected_list.at(i)), JsonStringEq(wrapAsObject(buffer.serialize())));
  }

  // Add a valid span. Valid means can be serialized to v2.
  EXPECT_TRUE(buffer.addSpan(createSpan({"cs"}, IpType::V4)));
  // While the span is valid, however the buffer is full.
  EXPECT_FALSE(buffer.addSpan(createSpan({"cs", "sr"}, IpType::V4)));

  buffer.clear();
  EXPECT_EQ(0ULL, buffer.pendingSpans());
  EXPECT_EQ("[]", buffer.serialize());
}

template <typename Type> std::string serializedMessageToJson(const std::string& serialized) {
  Type message;
  message.ParseFromString(serialized);
  std::string json;
  Protobuf::util::MessageToJsonString(message, &json);
  return json;
}

TEST(ZipkinSpanBufferTest, TestSerializeTimestamp) {
  const std::string default_timestamp_string = std::to_string(DEFAULT_TEST_TIMESTAMP);

  ProtobufWkt::Struct object;
  auto* fields = object.mutable_fields();
  Util::Replacements replacements;
  (*fields)["timestamp"] = Util::uint64Value(DEFAULT_TEST_TIMESTAMP, "timestamp", replacements);

  ASSERT_EQ(1, replacements.size());
  EXPECT_EQ(absl::StrCat("\"timestamp\":\"", default_timestamp_string, "\""),
            replacements.at(0).first);
  EXPECT_EQ(absl::StrCat("\"timestamp\":", default_timestamp_string), replacements.at(0).second);
}

TEST(ZipkinSpanBufferTest, ConstructBuffer) {
  const std::string expected1 =
      withDefaultTimestampAndDuration(R"([{"traceId":"0000000000000001",)"
                                      R"("name":"",)"
                                      R"("id":"0000000000000001",)"
                                      R"("duration":DEFAULT_TEST_DURATION,)"
                                      R"("annotations":[{"timestamp":DEFAULT_TEST_TIMESTAMP,)"
                                      R"("value":"cs",)"
                                      R"("endpoint":{"ipv4":"1.2.3.4",)"
                                      R"("port":8080,)"
                                      R"("serviceName":"service1"}},)"
                                      R"({"timestamp":DEFAULT_TEST_TIMESTAMP,)"
                                      R"("value":"sr",)"
                                      R"("endpoint":{"ipv4":"1.2.3.4",)"
                                      R"("port":8080,)"
                                      R"("serviceName":"service1"}}],)"
                                      R"("binaryAnnotations":[{"key":"response_size",)"
                                      R"("value":"DEFAULT_TEST_DURATION"}]}])");

  const std::string expected2 =
      withDefaultTimestampAndDuration(R"([{"traceId":"0000000000000001",)"
                                      R"("name":"",)"
                                      R"("id":"0000000000000001",)"
                                      R"("duration":DEFAULT_TEST_DURATION,)"
                                      R"("annotations":[{"timestamp":DEFAULT_TEST_TIMESTAMP,)"
                                      R"("value":"cs",)"
                                      R"("endpoint":{"ipv4":"1.2.3.4",)"
                                      R"("port":8080,)"
                                      R"("serviceName":"service1"}},)"
                                      R"({"timestamp":DEFAULT_TEST_TIMESTAMP,)"
                                      R"("value":"sr",)"
                                      R"("endpoint":{"ipv4":"1.2.3.4",)"
                                      R"("port":8080,)"
                                      R"("serviceName":"service1"}}],)"
                                      R"("binaryAnnotations":[{"key":"response_size",)"
                                      R"("value":"DEFAULT_TEST_DURATION"}]},)"
                                      R"({"traceId":"0000000000000001",)"
                                      R"("name":"",)"
                                      R"("id":"0000000000000001",)"
                                      R"("duration":DEFAULT_TEST_DURATION,)"
                                      R"("annotations":[{"timestamp":DEFAULT_TEST_TIMESTAMP,)"
                                      R"("value":"cs",)"
                                      R"("endpoint":{"ipv4":"1.2.3.4",)"
                                      R"("port":8080,)"
                                      R"("serviceName":"service1"}},)"
                                      R"({"timestamp":DEFAULT_TEST_TIMESTAMP,)"
                                      R"("value":"sr",)"
                                      R"("endpoint":{"ipv4":"1.2.3.4",)"
                                      R"("port":8080,)"
                                      R"("serviceName":"service1"}}],)"
                                      R"("binaryAnnotations":[{"key":"response_size",)"
                                      R"("value":"DEFAULT_TEST_DURATION"}]}])");
  const bool shared = true;
  const bool delay_allocation = true;

  SpanBuffer buffer1(envoy::config::trace::v3::ZipkinConfig::hidden_envoy_deprecated_HTTP_JSON_V1,
                     shared);
  expectSerializedBuffer(buffer1, delay_allocation, {expected1, expected2});

  // Prepare 3 slots, since we will add one more inside the `expectSerializedBuffer` function.
  SpanBuffer buffer2(envoy::config::trace::v3::ZipkinConfig::hidden_envoy_deprecated_HTTP_JSON_V1,
                     shared, 3);
  expectSerializedBuffer(buffer2, !delay_allocation, {expected1, expected2});
}

TEST(ZipkinSpanBufferTest, SerializeSpan) {
  const bool shared = true;
  SpanBuffer buffer1(envoy::config::trace::v3::ZipkinConfig::HTTP_JSON, shared, 2);
  buffer1.addSpan(createSpan({"cs"}, IpType::V4));
  EXPECT_THAT(wrapAsObject("[{"
                           R"("traceId":"0000000000000001",)"
                           R"("id":"0000000000000001",)"
                           R"("kind":"CLIENT",)"
                           R"("timestamp":DEFAULT_TEST_TIMESTAMP,)"
                           R"("duration":DEFAULT_TEST_DURATION,)"
                           R"("localEndpoint":{)"
                           R"("serviceName":"service1",)"
                           R"("ipv4":"1.2.3.4",)"
                           R"("port":8080},)"
                           R"("tags":{)"
                           R"("response_size":"DEFAULT_TEST_DURATION"},)"
                           "}]"),
              JsonStringEq(wrapAsObject(buffer1.serialize())));

  SpanBuffer buffer1_v6(envoy::config::trace::v3::ZipkinConfig::HTTP_JSON, shared, 2);
  buffer1_v6.addSpan(createSpan({"cs"}, IpType::V6));
  EXPECT_THAT(wrapAsObject("[{"
                           R"("traceId":"0000000000000001",)"
                           R"("id":"0000000000000001",)"
                           R"("kind":"CLIENT",)"
                           R"("timestamp":DEFAULT_TEST_TIMESTAMP,)"
                           R"("duration":DEFAULT_TEST_DURATION,)"
                           R"("localEndpoint":{)"
                           R"("serviceName":"service1",)"
                           R"("ipv6":"2001:db8:85a3::8a2e:370:4444",)"
                           R"("port":7334},)"
                           R"("tags":{)"
                           R"("response_size":"DEFAULT_TEST_DURATION"},)"
                           "}]"),
              JsonStringEq(wrapAsObject(buffer1_v6.serialize())));

  SpanBuffer buffer2(envoy::config::trace::v3::ZipkinConfig::HTTP_JSON, shared, 2);
  buffer2.addSpan(createSpan({"cs", "sr"}, IpType::V4));
  EXPECT_THAT(wrapAsObject("[{"
                           R"("traceId":"0000000000000001",)"
                           R"("id":"0000000000000001",)"
                           R"("kind":"CLIENT",)"
                           R"("timestamp":DEFAULT_TEST_TIMESTAMP,)"
                           R"("duration":DEFAULT_TEST_DURATION,)"
                           R"("localEndpoint":{)"
                           R"("serviceName":"service1",)"
                           R"("ipv4":"1.2.3.4",)"
                           R"("port":8080},)"
                           R"("tags":{)"
                           R"("response_size":"DEFAULT_TEST_DURATION"}},)"
                           R"({)"
                           R"("traceId":"0000000000000001",)"
                           R"("id":"0000000000000001",)"
                           R"("kind":"SERVER",)"
                           R"("timestamp":DEFAULT_TEST_TIMESTAMP,)"
                           R"("duration":DEFAULT_TEST_DURATION,)"
                           R"("localEndpoint":{)"
                           R"("serviceName":"service1",)"
                           R"("ipv4":"1.2.3.4",)"
                           R"("port":8080},)"
                           R"("tags":{)"
                           R"("response_size":"DEFAULT_TEST_DURATION"},)"
                           R"("shared":true)"
                           "}]"),
              JsonStringEq(wrapAsObject(buffer2.serialize())));

  SpanBuffer buffer3(envoy::config::trace::v3::ZipkinConfig::HTTP_JSON, !shared, 2);
  buffer3.addSpan(createSpan({"cs", "sr"}, IpType::V4));
  EXPECT_THAT(wrapAsObject("[{"
                           R"("traceId":"0000000000000001",)"
                           R"("id":"0000000000000001",)"
                           R"("kind":"CLIENT",)"
                           R"("timestamp":DEFAULT_TEST_TIMESTAMP,)"
                           R"("duration":DEFAULT_TEST_DURATION,)"
                           R"("localEndpoint":{)"
                           R"("serviceName":"service1",)"
                           R"("ipv4":"1.2.3.4",)"
                           R"("port":8080},)"
                           R"("tags":{)"
                           R"("response_size":"DEFAULT_TEST_DURATION"}},)"
                           R"({)"
                           R"("traceId":"0000000000000001",)"
                           R"("id":"0000000000000001",)"
                           R"("kind":"SERVER",)"
                           R"("timestamp":DEFAULT_TEST_TIMESTAMP,)"
                           R"("duration":DEFAULT_TEST_DURATION,)"
                           R"("localEndpoint":{)"
                           R"("serviceName":"service1",)"
                           R"("ipv4":"1.2.3.4",)"
                           R"("port":8080},)"
                           R"("tags":{)"
                           R"("response_size":"DEFAULT_TEST_DURATION"})"
                           "}]"),
              JsonStringEq(wrapAsObject(buffer3.serialize())));

  SpanBuffer buffer4(envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO, shared, 2);
  buffer4.addSpan(createSpan({"cs"}, IpType::V4));
  EXPECT_EQ(withDefaultTimestampAndDuration("{"
                                            R"("spans":[{)"
                                            R"("traceId":"AAAAAAAAAAE=",)"
                                            R"("id":"AQAAAAAAAAA=",)"
                                            R"("kind":"CLIENT",)"
                                            R"("timestamp":"DEFAULT_TEST_TIMESTAMP",)"
                                            R"("duration":"DEFAULT_TEST_DURATION",)"
                                            R"("localEndpoint":{)"
                                            R"("serviceName":"service1",)"
                                            R"("ipv4":"AQIDBA==",)"
                                            R"("port":8080},)"
                                            R"("tags":{)"
                                            R"("response_size":"DEFAULT_TEST_DURATION"})"
                                            "}]}"),
            serializedMessageToJson<zipkin::proto3::ListOfSpans>(buffer4.serialize()));

  SpanBuffer buffer4_v6(envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO, shared, 2);
  buffer4_v6.addSpan(createSpan({"cs"}, IpType::V6));
  EXPECT_EQ(withDefaultTimestampAndDuration("{"
                                            R"("spans":[{)"
                                            R"("traceId":"AAAAAAAAAAE=",)"
                                            R"("id":"AQAAAAAAAAA=",)"
                                            R"("kind":"CLIENT",)"
                                            R"("timestamp":"DEFAULT_TEST_TIMESTAMP",)"
                                            R"("duration":"DEFAULT_TEST_DURATION",)"
                                            R"("localEndpoint":{)"
                                            R"("serviceName":"service1",)"
                                            R"("ipv6":"IAENuIWjAAAAAIouA3BERA==",)"
                                            R"("port":7334},)"
                                            R"("tags":{)"
                                            R"("response_size":"DEFAULT_TEST_DURATION"})"
                                            "}]}"),
            serializedMessageToJson<zipkin::proto3::ListOfSpans>(buffer4_v6.serialize()));

  SpanBuffer buffer5(envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO, shared, 2);
  buffer5.addSpan(createSpan({"cs", "sr"}, IpType::V4));
  EXPECT_EQ(withDefaultTimestampAndDuration("{"
                                            R"("spans":[{)"
                                            R"("traceId":"AAAAAAAAAAE=",)"
                                            R"("id":"AQAAAAAAAAA=",)"
                                            R"("kind":"CLIENT",)"
                                            R"("timestamp":"DEFAULT_TEST_TIMESTAMP",)"
                                            R"("duration":"DEFAULT_TEST_DURATION",)"
                                            R"("localEndpoint":{)"
                                            R"("serviceName":"service1",)"
                                            R"("ipv4":"AQIDBA==",)"
                                            R"("port":8080},)"
                                            R"("tags":{)"
                                            R"("response_size":"DEFAULT_TEST_DURATION"}},)"
                                            R"({)"
                                            R"("traceId":"AAAAAAAAAAE=",)"
                                            R"("id":"AQAAAAAAAAA=",)"
                                            R"("kind":"SERVER",)"
                                            R"("timestamp":"DEFAULT_TEST_TIMESTAMP",)"
                                            R"("duration":"DEFAULT_TEST_DURATION",)"
                                            R"("localEndpoint":{)"
                                            R"("serviceName":"service1",)"
                                            R"("ipv4":"AQIDBA==",)"
                                            R"("port":8080},)"
                                            R"("tags":{)"
                                            R"("response_size":"DEFAULT_TEST_DURATION"},)"
                                            R"("shared":true)"
                                            "}]}"),
            serializedMessageToJson<zipkin::proto3::ListOfSpans>(buffer5.serialize()));

  SpanBuffer buffer6(envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO, !shared, 2);
  buffer6.addSpan(createSpan({"cs", "sr"}, IpType::V4));
  EXPECT_EQ(withDefaultTimestampAndDuration("{"
                                            R"("spans":[{)"
                                            R"("traceId":"AAAAAAAAAAE=",)"
                                            R"("id":"AQAAAAAAAAA=",)"
                                            R"("kind":"CLIENT",)"
                                            R"("timestamp":"DEFAULT_TEST_TIMESTAMP",)"
                                            R"("duration":"DEFAULT_TEST_DURATION",)"
                                            R"("localEndpoint":{)"
                                            R"("serviceName":"service1",)"
                                            R"("ipv4":"AQIDBA==",)"
                                            R"("port":8080},)"
                                            R"("tags":{)"
                                            R"("response_size":"DEFAULT_TEST_DURATION"}},)"
                                            R"({)"
                                            R"("traceId":"AAAAAAAAAAE=",)"
                                            R"("id":"AQAAAAAAAAA=",)"
                                            R"("kind":"SERVER",)"
                                            R"("timestamp":"DEFAULT_TEST_TIMESTAMP",)"
                                            R"("duration":"DEFAULT_TEST_DURATION",)"
                                            R"("localEndpoint":{)"
                                            R"("serviceName":"service1",)"
                                            R"("ipv4":"AQIDBA==",)"
                                            R"("port":8080},)"
                                            R"("tags":{)"
                                            R"("response_size":"DEFAULT_TEST_DURATION"})"
                                            "}]}"),
            serializedMessageToJson<zipkin::proto3::ListOfSpans>(buffer6.serialize()));
}

TEST(ZipkinSpanBufferTest, TestSerializeTimestampInTheFuture) {
  ProtobufWkt::Struct objectWithScientificNotation;
  auto* objectWithScientificNotationFields = objectWithScientificNotation.mutable_fields();
  (*objectWithScientificNotationFields)["timestamp"] = ValueUtil::numberValue(
      DEFAULT_TEST_TIMESTAMP); // the value of DEFAULT_TEST_TIMESTAMP is 1584324295476870.
  const auto objectWithScientificNotationJson =
      MessageUtil::getJsonStringFromMessageOrDie(objectWithScientificNotation, false, true);
  // Since we use ValueUtil::numberValue to set the timestamp, we expect to
  // see the value is rendered with scientific notation (1.58432429547687e+15).
  EXPECT_EQ(R"({"timestamp":1.58432429547687e+15})", objectWithScientificNotationJson);

  ProtobufWkt::Struct object;
  auto* objectFields = object.mutable_fields();
  Util::Replacements replacements;
  (*objectFields)["timestamp"] =
      Util::uint64Value(DEFAULT_TEST_TIMESTAMP, "timestamp", replacements);
  const auto objectJson = MessageUtil::getJsonStringFromMessageOrDie(object, false, true);
  // We still have "1584324295476870" from MessageUtil::getJsonStringFromMessageOrDie here.
  EXPECT_EQ(R"({"timestamp":"1584324295476870"})", objectJson);
  // However, then the replacement correctly replaces "1584324295476870" with 1584324295476870
  // (without quotes).
  EXPECT_EQ(R"({"timestamp":1584324295476870})", absl::StrReplaceAll(objectJson, replacements));

  SpanBuffer bufferDeprecatedJsonV1(envoy::config::trace::v3::ZipkinConfig::HTTP_JSON, true, 2);
  bufferDeprecatedJsonV1.addSpan(createSpan({"cs"}, IpType::V4));
  // We do "HasSubstr" here since we could not compare the serialized JSON of a ProtobufWkt::Struct
  // object, since the positions of keys are not consistent between calls.
  EXPECT_THAT(bufferDeprecatedJsonV1.serialize(), HasSubstr(R"("timestamp":1584324295476870)"));
  EXPECT_THAT(bufferDeprecatedJsonV1.serialize(),
              Not(HasSubstr(R"("timestamp":1.58432429547687e+15)")));
  EXPECT_THAT(bufferDeprecatedJsonV1.serialize(),
              Not(HasSubstr(R"("timestamp":"1584324295476870")")));
  EXPECT_THAT(bufferDeprecatedJsonV1.serialize(), HasSubstr(R"("duration":2584324295476870)"));
  EXPECT_THAT(bufferDeprecatedJsonV1.serialize(),
              Not(HasSubstr(R"("duration":2.584324295476870e+15)")));
  EXPECT_THAT(bufferDeprecatedJsonV1.serialize(),
              Not(HasSubstr(R"("duration":"2584324295476870")")));

  SpanBuffer bufferJsonV2(
      envoy::config::trace::v3::ZipkinConfig::hidden_envoy_deprecated_HTTP_JSON_V1, true, 2);
  bufferJsonV2.addSpan(createSpan({"cs"}, IpType::V4));
  EXPECT_THAT(bufferJsonV2.serialize(), HasSubstr(R"("timestamp":1584324295476870)"));
  EXPECT_THAT(bufferJsonV2.serialize(), Not(HasSubstr(R"("timestamp":1.58432429547687e+15)")));
  EXPECT_THAT(bufferJsonV2.serialize(), Not(HasSubstr(R"("timestamp":"1584324295476870")")));
  EXPECT_THAT(bufferJsonV2.serialize(), HasSubstr(R"("duration":2584324295476870)"));
  EXPECT_THAT(bufferJsonV2.serialize(), Not(HasSubstr(R"("duration":2.584324295476870e+15)")));
  EXPECT_THAT(bufferJsonV2.serialize(), Not(HasSubstr(R"("duration":"2584324295476870")")));
}

} // namespace
} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
