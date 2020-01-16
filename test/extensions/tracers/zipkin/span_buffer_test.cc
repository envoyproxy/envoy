#include "envoy/config/trace/v3/trace.pb.h"

#include "common/network/utility.h"

#include "extensions/tracers/zipkin/span_buffer.h"

#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_format.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {
namespace {

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
  annotation.setTimestamp(1566058071601051);
  annotation.setEndpoint(createEndpoint(ip_type));
  return annotation;
}

BinaryAnnotation createTag() {
  BinaryAnnotation tag;
  tag.setKey("component");
  tag.setValue("proxy");
  return tag;
}

Span createSpan(const std::vector<absl::string_view>& annotation_values, const IpType ip_type) {
  DangerousDeprecatedTestTime test_time;
  Span span(test_time.timeSystem());
  span.setId(1);
  span.setTraceId(1);
  span.setDuration(100);
  std::vector<Annotation> annotations;
  annotations.reserve(annotation_values.size());
  for (absl::string_view value : annotation_values) {
    annotations.push_back(createAnnotation(value, ip_type));
  }
  span.setAnnotations(annotations);
  span.setBinaryAnnotations({createTag()});
  return span;
}

// To wrap JSON array string in a object for JSON string comparison through JsonStringEq test
// utility.
std::string wrapAsObject(absl::string_view array_string) {
  return absl::StrFormat(R"({"root":%s})", array_string);
}

void expectSerializedBuffer(SpanBuffer& buffer, const bool delay_allocation,
                            const std::vector<std::string>& expected_list) {
  DangerousDeprecatedTestTime test_time;

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

TEST(ZipkinSpanBufferTest, ConstructBuffer) {
  const std::string expected1 = R"([{"traceId":"0000000000000001",)"
                                R"("name":"",)"
                                R"("id":"0000000000000001",)"
                                R"("duration":100,)"
                                R"("annotations":[{"timestamp":1566058071601051,)"
                                R"("value":"cs",)"
                                R"("endpoint":{"ipv4":"1.2.3.4",)"
                                R"("port":8080,)"
                                R"("serviceName":"service1"}},)"
                                R"({"timestamp":1566058071601051,)"
                                R"("value":"sr",)"
                                R"("endpoint":{"ipv4":"1.2.3.4",)"
                                R"("port":8080,)"
                                R"("serviceName":"service1"}}],)"
                                R"("binaryAnnotations":[{"key":"component",)"
                                R"("value":"proxy"}]}])";

  const std::string expected2 = R"([{"traceId":"0000000000000001",)"
                                R"("name":"",)"
                                R"("id":"0000000000000001",)"
                                R"("duration":100,)"
                                R"("annotations":[{"timestamp":1566058071601051,)"
                                R"("value":"cs",)"
                                R"("endpoint":{"ipv4":"1.2.3.4",)"
                                R"("port":8080,)"
                                R"("serviceName":"service1"}},)"
                                R"({"timestamp":1566058071601051,)"
                                R"("value":"sr",)"
                                R"("endpoint":{"ipv4":"1.2.3.4",)"
                                R"("port":8080,)"
                                R"("serviceName":"service1"}}],)"
                                R"("binaryAnnotations":[{"key":"component",)"
                                R"("value":"proxy"}]},)"
                                R"({"traceId":"0000000000000001",)"
                                R"("name":"",)"
                                R"("id":"0000000000000001",)"
                                R"("duration":100,)"
                                R"("annotations":[{"timestamp":1566058071601051,)"
                                R"("value":"cs",)"
                                R"("endpoint":{"ipv4":"1.2.3.4",)"
                                R"("port":8080,)"
                                R"("serviceName":"service1"}},)"
                                R"({"timestamp":1566058071601051,)"
                                R"("value":"sr",)"
                                R"("endpoint":{"ipv4":"1.2.3.4",)"
                                R"("port":8080,)"
                                R"("serviceName":"service1"}}],)"
                                R"("binaryAnnotations":[{"key":"component",)"
                                R"("value":"proxy"}]}])";
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
                           R"("timestamp":1566058071601051,)"
                           R"("duration":100,)"
                           R"("localEndpoint":{)"
                           R"("serviceName":"service1",)"
                           R"("ipv4":"1.2.3.4",)"
                           R"("port":8080},)"
                           R"("tags":{)"
                           R"("component":"proxy"})"
                           "}]"),
              JsonStringEq(wrapAsObject(buffer1.serialize())));

  SpanBuffer buffer1_v6(envoy::config::trace::v3::ZipkinConfig::HTTP_JSON, shared, 2);
  buffer1_v6.addSpan(createSpan({"cs"}, IpType::V6));
  EXPECT_THAT(wrapAsObject("[{"
                           R"("traceId":"0000000000000001",)"
                           R"("id":"0000000000000001",)"
                           R"("kind":"CLIENT",)"
                           R"("timestamp":1566058071601051,)"
                           R"("duration":100,)"
                           R"("localEndpoint":{)"
                           R"("serviceName":"service1",)"
                           R"("ipv6":"2001:db8:85a3::8a2e:370:4444",)"
                           R"("port":7334},)"
                           R"("tags":{)"
                           R"("component":"proxy"})"
                           "}]"),
              JsonStringEq(wrapAsObject(buffer1_v6.serialize())));

  SpanBuffer buffer2(envoy::config::trace::v3::ZipkinConfig::HTTP_JSON, shared, 2);
  buffer2.addSpan(createSpan({"cs", "sr"}, IpType::V4));
  EXPECT_THAT(wrapAsObject("[{"
                           R"("traceId":"0000000000000001",)"
                           R"("id":"0000000000000001",)"
                           R"("kind":"CLIENT",)"
                           R"("timestamp":1566058071601051,)"
                           R"("duration":100,)"
                           R"("localEndpoint":{)"
                           R"("serviceName":"service1",)"
                           R"("ipv4":"1.2.3.4",)"
                           R"("port":8080},)"
                           R"("tags":{)"
                           R"("component":"proxy"}},)"
                           R"({)"
                           R"("traceId":"0000000000000001",)"
                           R"("id":"0000000000000001",)"
                           R"("kind":"SERVER",)"
                           R"("timestamp":1566058071601051,)"
                           R"("duration":100,)"
                           R"("localEndpoint":{)"
                           R"("serviceName":"service1",)"
                           R"("ipv4":"1.2.3.4",)"
                           R"("port":8080},)"
                           R"("tags":{)"
                           R"("component":"proxy"},)"
                           R"("shared":true)"
                           "}]"),
              JsonStringEq(wrapAsObject(buffer2.serialize())));

  SpanBuffer buffer3(envoy::config::trace::v3::ZipkinConfig::HTTP_JSON, !shared, 2);
  buffer3.addSpan(createSpan({"cs", "sr"}, IpType::V4));
  EXPECT_THAT(wrapAsObject("[{"
                           R"("traceId":"0000000000000001",)"
                           R"("id":"0000000000000001",)"
                           R"("kind":"CLIENT",)"
                           R"("timestamp":1566058071601051,)"
                           R"("duration":100,)"
                           R"("localEndpoint":{)"
                           R"("serviceName":"service1",)"
                           R"("ipv4":"1.2.3.4",)"
                           R"("port":8080},)"
                           R"("tags":{)"
                           R"("component":"proxy"}},)"
                           R"({)"
                           R"("traceId":"0000000000000001",)"
                           R"("id":"0000000000000001",)"
                           R"("kind":"SERVER",)"
                           R"("timestamp":1566058071601051,)"
                           R"("duration":100,)"
                           R"("localEndpoint":{)"
                           R"("serviceName":"service1",)"
                           R"("ipv4":"1.2.3.4",)"
                           R"("port":8080},)"
                           R"("tags":{)"
                           R"("component":"proxy"})"
                           "}]"),
              JsonStringEq(wrapAsObject(buffer3.serialize())));

  SpanBuffer buffer4(envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO, shared, 2);
  buffer4.addSpan(createSpan({"cs"}, IpType::V4));
  EXPECT_EQ("{"
            R"("spans":[{)"
            R"("traceId":"AAAAAAAAAAE=",)"
            R"("id":"AQAAAAAAAAA=",)"
            R"("kind":"CLIENT",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"AQIDBA==",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"})"
            "}]}",
            serializedMessageToJson<zipkin::proto3::ListOfSpans>(buffer4.serialize()));

  SpanBuffer buffer4_v6(envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO, shared, 2);
  buffer4_v6.addSpan(createSpan({"cs"}, IpType::V6));
  EXPECT_EQ("{"
            R"("spans":[{)"
            R"("traceId":"AAAAAAAAAAE=",)"
            R"("id":"AQAAAAAAAAA=",)"
            R"("kind":"CLIENT",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv6":"IAENuIWjAAAAAIouA3BERA==",)"
            R"("port":7334},)"
            R"("tags":{)"
            R"("component":"proxy"})"
            "}]}",
            serializedMessageToJson<zipkin::proto3::ListOfSpans>(buffer4_v6.serialize()));

  SpanBuffer buffer5(envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO, shared, 2);
  buffer5.addSpan(createSpan({"cs", "sr"}, IpType::V4));
  EXPECT_EQ("{"
            R"("spans":[{)"
            R"("traceId":"AAAAAAAAAAE=",)"
            R"("id":"AQAAAAAAAAA=",)"
            R"("kind":"CLIENT",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"AQIDBA==",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"}},)"
            R"({)"
            R"("traceId":"AAAAAAAAAAE=",)"
            R"("id":"AQAAAAAAAAA=",)"
            R"("kind":"SERVER",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"AQIDBA==",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"},)"
            R"("shared":true)"
            "}]}",
            serializedMessageToJson<zipkin::proto3::ListOfSpans>(buffer5.serialize()));

  SpanBuffer buffer6(envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO, !shared, 2);
  buffer6.addSpan(createSpan({"cs", "sr"}, IpType::V4));
  EXPECT_EQ("{"
            R"("spans":[{)"
            R"("traceId":"AAAAAAAAAAE=",)"
            R"("id":"AQAAAAAAAAA=",)"
            R"("kind":"CLIENT",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"AQIDBA==",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"}},)"
            R"({)"
            R"("traceId":"AAAAAAAAAAE=",)"
            R"("id":"AQAAAAAAAAA=",)"
            R"("kind":"SERVER",)"
            R"("timestamp":"1566058071601051",)"
            R"("duration":"100",)"
            R"("localEndpoint":{)"
            R"("serviceName":"service1",)"
            R"("ipv4":"AQIDBA==",)"
            R"("port":8080},)"
            R"("tags":{)"
            R"("component":"proxy"})"
            "}]}",
            serializedMessageToJson<zipkin::proto3::ListOfSpans>(buffer6.serialize()));
}

} // namespace
} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
