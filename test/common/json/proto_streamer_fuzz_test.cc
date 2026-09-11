#include <array>
#include <tuple>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/json/proto_streamer.h"
#include "source/common/protobuf/utility.h"
#include "source/common/protobuf/visitor_helper.h"

#include "test/common/json/json_sanitizer_test_util.h"
#include "test/common/json/proto_streamer_test.pb.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/test_common/utility.h"

#include "absl/base/attributes.h"
#include "absl/strings/str_cat.h"
#include "xds/type/v3/typed_struct.pb.h"

namespace Envoy {
namespace Json {
namespace {

using ::test::common::json::TestMessage;

constexpr std::array KnownTypeUrls = {
    "type.googleapis.com/google.protobuf.Empty",
    "type.googleapis.com/google.protobuf.Duration",
    "type.googleapis.com/google.protobuf.Timestamp",
    "type.googleapis.com/google.protobuf.Struct",
    "type.googleapis.com/google.protobuf.Value",
    "type.googleapis.com/google.protobuf.ListValue",
    "type.googleapis.com/google.protobuf.DoubleValue",
    "type.googleapis.com/google.protobuf.FloatValue",
    "type.googleapis.com/google.protobuf.Int64Value",
    "type.googleapis.com/google.protobuf.UInt64Value",
    "type.googleapis.com/google.protobuf.Int32Value",
    "type.googleapis.com/google.protobuf.UInt32Value",
    "type.googleapis.com/google.protobuf.BoolValue",
    "type.googleapis.com/google.protobuf.StringValue",
    "type.googleapis.com/google.protobuf.BytesValue",
    "type.googleapis.com/google.protobuf.Any",
    "type.googleapis.com/xds.type.v3.TypedStruct",
    "type.googleapis.com/udpa.type.v1.TypedStruct",
    "type.googleapis.com/test.common.json.TestNested",
    "type.googleapis.com/envoy.test.Sensitive",
    "type.googleapis.com/test.common.json.TestMessage",
};

void removeUnserializableUtf8Strings(Protobuf::Message& message,
                                     const Protobuf::FieldDescriptor& field) {
  const Protobuf::Reflection& reflection = *message.GetReflection();
  std::string scratch;
  if (!field.is_repeated()) {
    if (!TestUtil::isProtoSerializableUtf8(
            reflection.GetStringReference(message, &field, &scratch))) {
      reflection.SetString(&message, &field, "");
    }
    return;
  }
  const int size = reflection.FieldSize(message, &field);
  for (int i = 0; i < size; ++i) {
    if (!TestUtil::isProtoSerializableUtf8(
            reflection.GetRepeatedStringReference(message, &field, i, &scratch))) {
      reflection.SetRepeatedString(&message, &field, i, "");
    }
  }
}

void makeComparable(Protobuf::Message& message);

void makeAnyComparable(Protobuf::Any& any) {
  ProtobufTypes::MessagePtr packed = ProtobufMessage::Helper::typeUrlToMessage(any.type_url());
  if (packed == nullptr) {
    return;
  }
  std::ignore = packed->ParsePartialFromString(any.value());
  makeComparable(*packed);
  // Writing back what parsed makes the payload well formed.
  any.set_value(packed->SerializeAsString());
}

// Repairs at generation, because excluding an input at comparison time would take the whole
// message with it.
void makeComparable(Protobuf::Message& message) {
  const Protobuf::Reflection& reflection = *message.GetReflection();
  std::vector<const Protobuf::FieldDescriptor*> fields;
  reflection.ListFields(message, &fields);
  for (const Protobuf::FieldDescriptor* field : fields) {
    // Bytes are base64 whatever they hold, only strings can be spelled two ways.
    if (field->type() == Protobuf::FieldDescriptor::TYPE_STRING) {
      removeUnserializableUtf8Strings(message, *field);
      continue;
    }
    if (field->cpp_type() != Protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      continue;
    }
    const int size = field->is_repeated() ? reflection.FieldSize(message, field) : 1;
    for (int i = 0; i < size; ++i) {
      makeComparable(field->is_repeated() ? *reflection.MutableRepeatedMessage(&message, field, i)
                                          : *reflection.MutableMessage(&message, field));
    }
  }

  Protobuf::Any* any = Protobuf::DynamicCastMessage<Protobuf::Any>(&message);
  if (any != nullptr) {
    makeAnyComparable(*any);
  }
}

std::string streamMessage(const Protobuf::Message& message, bool redact) {
  Buffer::OwnedImpl buffer;
  {
    BufferStreamer streamer(buffer);
    BufferStreamer::ArrayPtr array = streamer.makeRootArray();
    MessageStreamer message_streamer(
        message, *array, {.preserve_proto_field_names_ = true, .redact_sensitive_fields_ = redact});
    while (message_streamer.next()) {
    }
  }
  return buffer.toString();
}

void checkAgainstPrinter(const Protobuf::Message& input, bool redact) {
  // Copying also folds duplicate map keys, which JsonStringToMessage rejects in either output.
  ProtobufTypes::MessagePtr message(input.New());
  message->CopyFrom(input);

  const std::string streamed = streamMessage(*message, redact);
  if (redact) {
    MessageUtil::redact(*message);
  }
  Protobuf::ListValue streamed_json;
  const bool streamed_parses = Protobuf::util::JsonStringToMessage(streamed, &streamed_json).ok();

  const absl::StatusOr<std::string> printed = MessageUtil::getJsonStringFromMessage(*message);
  if (!printed.ok()) {
    // The streamer has no way to refuse a message, unlike the printer.
    RELEASE_ASSERT(streamed_parses, absl::StrCat("printer failed: ", printed.status().ToString(),
                                                 "\nstreamed: ", streamed));
    return;
  }

  // The streamer writes a root array, so the printed message is wrapped in one to compare.
  const std::string wrapped = absl::StrCat("[", *printed, "]");
  Protobuf::ListValue printed_json;
  if (!streamed_parses || !Protobuf::util::JsonStringToMessage(wrapped, &printed_json).ok()) {
    // With one output unparseable there is nothing left to compare but the bytes.
    RELEASE_ASSERT(wrapped == streamed, absl::StrCat("unparseable and unalike\nprinted: ", *printed,
                                                     "\nstreamed: ", streamed));
    return;
  }

  RELEASE_ASSERT(TestUtility::protoEqual(printed_json, streamed_json),
                 absl::StrCat("printed: ", *printed, "\nstreamed: ", streamed));
}

DEFINE_PROTO_FUZZER(const TestMessage& input) {
  ABSL_ATTRIBUTE_UNUSED static protobuf_mutator::libfuzzer::PostProcessorRegistration<TestMessage>
      comparable = {[](TestMessage* message, unsigned int) { makeComparable(*message); }};

  // Resolvable type urls, save for one in 128 the printer will refuse.
  ABSL_ATTRIBUTE_UNUSED static protobuf_mutator::libfuzzer::PostProcessorRegistration<Protobuf::Any>
      any_type_url = {[](Protobuf::Any* any, unsigned int seed) {
        if (any->type_url().empty() && any->value().empty()) {
          return;
        }
        if (ProtobufMessage::Helper::typeUrlToMessage(any->type_url()) == nullptr &&
            seed % 128 != 0) {
          any->set_type_url(KnownTypeUrls[seed % KnownTypeUrls.size()]);
          any->clear_value();
        }
        // Nothing orders this against makeComparable, so what it changed is repaired here.
        makeAnyComparable(*any);
      }};

  // Resolvable type urls, save for one in eight nothing reifies.
  ABSL_ATTRIBUTE_UNUSED static protobuf_mutator::libfuzzer::PostProcessorRegistration<
      xds::type::v3::TypedStruct>
      typed_struct_type_url = {[](xds::type::v3::TypedStruct* typed_struct, unsigned int seed) {
        if (ProtobufMessage::Helper::typeUrlToMessage(typed_struct->type_url()) != nullptr ||
            seed % 8 == 0) {
          return;
        }
        typed_struct->set_type_url(KnownTypeUrls[seed % KnownTypeUrls.size()]);
      }};

  for (const bool redact : {false, true}) {
    checkAgainstPrinter(input, redact);
  }
}

} // namespace
} // namespace Json
} // namespace Envoy
