#include "source/common/buffer/buffer_impl.h"
#include "source/common/json/proto_streamer.h"
#include "source/common/protobuf/utility.h"

#include "test/common/json/proto_streamer_test.pb.h"
#include "test/proto/sensitive.pb.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Json {
namespace {

using ::test::common::json::TestMessage;
using ::test::common::json::TestNested;

std::pair<std::string, uint32_t> stream(const Protobuf::Message& message,
                                        MessageStreamer::Options options = {
                                            .preserve_proto_field_names_ = true}) {
  std::string emitted;
  uint32_t pieces = 0;
  Buffer::OwnedImpl buffer;
  {
    BufferStreamer streamer(buffer);
    BufferStreamer::ArrayPtr array = streamer.makeRootArray();
    MessageStreamer message_streamer(message, *array, options);
    while (message_streamer.next()) {
      emitted += buffer.toString();
      buffer.drain(buffer.length());
      ++pieces;
    }
  }
  emitted += buffer.toString();
  return {emitted, pieces};
}

std::string printedInArray(const Protobuf::Message& message) {
  return absl::StrCat("[", MessageUtil::getJsonStringFromMessageOrError(message), "]");
}

void expectSameJson(const Protobuf::Message& message) {
  EXPECT_EQ(printedInArray(message), stream(message).first) << message.DebugString();
}

TEST(MessageStreamerTest, Empty) { expectSameJson(TestMessage()); }

TEST(MessageStreamerTest, Scalars) {
  TestMessage message;
  message.set_int32_value(-1);
  message.set_int64_value(std::numeric_limits<int64_t>::min());
  message.set_uint32_value(1);
  message.set_uint64_value(std::numeric_limits<uint64_t>::max());
  message.set_sint32_value(-7);
  message.set_sint64_value(-6);
  message.set_fixed32_value(3);
  message.set_fixed64_value(2);
  message.set_sfixed32_value(-5);
  message.set_sfixed64_value(-4);
  message.set_double_value(1.5);
  message.set_float_value(0.25);
  message.set_bool_value(true);
  message.set_string_value("\"quoted\"\n");
  message.add_repeated_int64(std::numeric_limits<int64_t>::max());
  message.add_repeated_uint64(std::numeric_limits<uint64_t>::max());
  message.add_repeated_double(140);
  message.add_repeated_strings("a");
  expectSameJson(message);
}

TEST(MessageStreamerTest, Enums) {
  TestMessage message;
  message.set_enum_value(::test::common::json::BETA);
  message.add_repeated_enum(::test::common::json::ALPHA);
  message.add_repeated_enum(static_cast<::test::common::json::TestEnum>(1234));
  expectSameJson(message);
}

TEST(MessageStreamerTest, BytesAreBase64) {
  TestMessage message;
  message.set_bytes_value(std::string("\x00\x01\xff", 3));
  message.mutable_nested()->add_objects("padding-check");
  expectSameJson(message);
}

TEST(MessageStreamerTest, NestedMessages) {
  TestMessage message;
  message.mutable_nested()->set_ratio(0.5);
  message.add_repeated_nested()->set_name("first");
  message.add_repeated_nested()->set_name("second");
  expectSameJson(message);
}

TEST(MessageStreamerTest, Maps) {
  TestMessage message;
  (*message.mutable_bool_keyed())[true] = "bool";
  (*message.mutable_int32_keyed())[-1] = "int32";
  (*message.mutable_int64_keyed())[std::numeric_limits<int64_t>::min()] = "int64";
  (*message.mutable_uint32_keyed())[1] = "uint32";
  (*message.mutable_uint64_keyed())[std::numeric_limits<uint64_t>::max()] = "uint64";
  (*message.mutable_string_keyed())["key"] = "string";
  (*message.mutable_message_valued())["key"].set_name("nested");
  expectSameJson(message);
}

TEST(MessageStreamerTest, Wrappers) {
  TestMessage message;
  message.mutable_wrapped_double()->set_value(1.5);
  message.mutable_wrapped_float()->set_value(0.25);
  message.mutable_wrapped_int64()->set_value(std::numeric_limits<int64_t>::min());
  message.mutable_wrapped_uint64()->set_value(std::numeric_limits<uint64_t>::max());
  message.mutable_wrapped_int32()->set_value(-1);
  message.mutable_wrapped_uint32()->set_value(42);
  message.mutable_wrapped_bool()->set_value(true);
  message.mutable_wrapped_string()->set_value("wrapped");
  message.mutable_wrapped_bytes()->set_value("wrapped");
  expectSameJson(message);
}

TEST(MessageStreamerTest, LowerCamelCaseKeys) {
  TestMessage message;
  message.set_int64_value(7);
  message.mutable_nested()->set_name("nested_name");
  message.add_repeated_strings("foo");

  const Protobuf::util::JsonPrintOptions options;
  std::string printed;
  ASSERT_TRUE(Protobuf::util::MessageToJsonString(message, &printed, options).ok());
  EXPECT_EQ(absl::StrCat("[", printed, "]"),
            stream(message, {.preserve_proto_field_names_ = false}).first);
}

TEST(MessageStreamerTest, DurationsAndTimestamps) {
  TestMessage message;
  message.mutable_duration()->set_nanos(1234);
  message.mutable_timestamp()->set_seconds(1234);
  message.mutable_timestamp()->set_nanos(5678);
  expectSameJson(message);

  TestMessage zeroed;
  zeroed.mutable_duration()->set_seconds(5678);
  zeroed.mutable_timestamp();
  expectSameJson(zeroed);
}

TEST(MessageStreamerTest, Anys) {
  TestMessage message;
  TestNested nested;
  nested.set_name("nested_name");
  nested.set_ratio(0.5);
  std::ignore = message.mutable_any()->PackFrom(nested);
  std::ignore = message.add_repeated_any()->PackFrom(TestNested());
  expectSameJson(message);
}

TEST(MessageStreamerTest, AnyOfWellKnownType) {
  TestMessage message;
  Protobuf::Duration duration;
  duration.set_seconds(3);
  std::ignore = message.mutable_any()->PackFrom(duration);
  Protobuf::Struct structured;
  (*structured.mutable_fields())["key"].set_string_value("value");
  std::ignore = message.add_repeated_any()->PackFrom(structured);
  expectSameJson(message);
}

TEST(MessageStreamerTest, AnyOfUnknownType) {
  TestMessage message;
  message.mutable_any()->set_type_url("type.googleapis.com/test.common.json.NotLinkedIn");
  EXPECT_EQ(R"([{"any":{"@type":"type.googleapis.com/test.common.json.NotLinkedIn"}}])",
            stream(message).first);
}

TEST(MessageStreamerTest, DynamicMessages) {
  TestMessage generated;
  generated.mutable_duration()->set_seconds(5);
  generated.mutable_timestamp()->set_seconds(1755561600);
  generated.mutable_nested()->set_name("nested_name");
  std::ignore = generated.mutable_any()->PackFrom(TestNested());

  std::string serialized;
  ASSERT_TRUE(generated.SerializeToString(&serialized));

  Protobuf::DynamicMessageFactory factory;
  ProtobufTypes::MessagePtr dynamic(factory.GetPrototype(TestMessage::descriptor())->New());
  ASSERT_TRUE(dynamic->ParseFromString(serialized));
  expectSameJson(*dynamic);
}

TEST(MessageStreamerTest, Structs) {
  TestMessage message;
  (*message.mutable_structured()->mutable_fields())["key"].set_string_value("value");
  (*message.mutable_structured()->mutable_fields())["number"].set_number_value(1.5);
  expectSameJson(message);
}

TEST(MessageStreamerTest, MessageThePrinterRejects) {
  TestMessage message;
  // The printer refuses NaN in a google.protobuf.Value.
  (*message.mutable_structured()->mutable_fields())["key"].set_number_value(
      std::numeric_limits<double>::quiet_NaN());
  EXPECT_EQ(R"([{"structured":null}])", stream(message).first);
}

TEST(MessageStreamerTest, NonFiniteNumbers) {
  TestMessage message;
  message.add_repeated_double(std::numeric_limits<double>::quiet_NaN());
  message.add_repeated_double(std::numeric_limits<double>::infinity());
  message.add_repeated_double(-std::numeric_limits<double>::infinity());
  expectSameJson(message);
}

void expectSameRedactedJson(const envoy::test::Sensitive& message) {
  envoy::test::Sensitive redacted = message;
  MessageUtil::redact(redacted);
  EXPECT_EQ(printedInArray(redacted),
            stream(message, {.preserve_proto_field_names_ = true, .redact_sensitive_fields_ = true})
                .first)
      << message.DebugString();
}

TEST(MessageStreamerTest, Redacts) {
  envoy::test::Sensitive sensitive;
  sensitive.set_sensitive_string("secret");
  sensitive.add_sensitive_repeated_string("secret");
  sensitive.set_sensitive_bytes("secret");
  sensitive.add_sensitive_repeated_bytes("secret");
  sensitive.set_sensitive_int(1);
  sensitive.add_sensitive_repeated_int(2);
  (*sensitive.mutable_sensitive_string_map())["key"] = "secret";
  (*sensitive.mutable_sensitive_int_map())["key"] = 3;
  sensitive.mutable_sensitive_wrapped_int()->set_value(6);
  sensitive.mutable_sensitive_wrapped_string()->set_value("secret");

  sensitive.set_insensitive_string("public");
  sensitive.add_insensitive_repeated_string("public");
  sensitive.set_insensitive_bytes("public");
  sensitive.set_insensitive_int(4);
  (*sensitive.mutable_insensitive_string_map())["key"] = "public";
  (*sensitive.mutable_insensitive_int_map())["key"] = 5;
  sensitive.mutable_insensitive_wrapped_int()->set_value(7);
  sensitive.mutable_insensitive_wrapped_string()->set_value("public");
  expectSameRedactedJson(sensitive);
}

TEST(MessageStreamerTest, RedactsNestedMessages) {
  envoy::test::Sensitive sensitive;
  // Everything under a sensitive field should be redacted.
  sensitive.mutable_sensitive_message()->set_insensitive_string("public");
  sensitive.mutable_sensitive_message()->set_insensitive_int(1);
  sensitive.add_sensitive_repeated_message()->set_insensitive_string("public");
  // Only the annotated fields of an insensitive message should be redacted.
  sensitive.mutable_insensitive_message()->set_sensitive_string("secret");
  sensitive.mutable_insensitive_message()->set_insensitive_string("public");
  sensitive.add_insensitive_repeated_message()->set_sensitive_int(2);
  expectSameRedactedJson(sensitive);
}

TEST(MessageStreamerTest, RedactsInsideAny) {
  envoy::test::Sensitive packed;
  packed.set_sensitive_string("secret");
  packed.set_insensitive_string("public");

  envoy::test::Sensitive sensitive;
  std::ignore = sensitive.mutable_insensitive_any()->PackFrom(packed);
  std::ignore = sensitive.mutable_sensitive_any()->PackFrom(packed);
  std::ignore = sensitive.add_insensitive_repeated_any()->PackFrom(packed);
  expectSameRedactedJson(sensitive);
}

TEST(MessageStreamerTest, RedactsInsideTypedStruct) {
  envoy::test::Sensitive sensitive;
  TestUtility::loadFromYaml(R"EOF(
type_url: type.googleapis.com/envoy.test.Sensitive
value:
  sensitive_string: secret
)EOF",
                            *sensitive.mutable_insensitive_typed_struct());
  TestUtility::loadFromYaml(R"EOF(
type_url: type.googleapis.com/envoy.test.Sensitive
value:
  insensitive_string: public
)EOF",
                            *sensitive.mutable_sensitive_typed_struct());
  expectSameRedactedJson(sensitive);
}

TEST(MessageStreamerTest, EmitsInPieces) {
  TestMessage message;
  message.mutable_nested()->set_name("nested_name");
  message.add_repeated_strings("one");
  message.add_repeated_strings("two");
  message.add_repeated_strings("three");

  const std::pair<std::string, uint32_t> streamed = stream(message);
  EXPECT_EQ(printedInArray(message), streamed.first);
  EXPECT_LT(5U, streamed.second);
}

TEST(MessageStreamerTest, DestroyedMidStream) {
  TestMessage message;
  message.mutable_nested()->set_name("nested_name");
  message.add_repeated_nested()->set_name("more");

  Buffer::OwnedImpl buffer;
  {
    BufferStreamer streamer(buffer);
    BufferStreamer::ArrayPtr array = streamer.makeRootArray();
    MessageStreamer message_streamer(message, *array, {.preserve_proto_field_names_ = true});
    ASSERT_TRUE(message_streamer.next());
    ASSERT_TRUE(message_streamer.next());
  }

  const std::string emitted = buffer.toString();
  EXPECT_EQ('[', emitted.front());
  EXPECT_EQ(']', emitted.back());
}

} // namespace
} // namespace Json
} // namespace Envoy
