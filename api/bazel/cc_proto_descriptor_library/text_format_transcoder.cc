#include "bazel/cc_proto_descriptor_library/text_format_transcoder.h"

#include <memory>
#include <string>

#include "absl/memory/memory.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "bazel/cc_proto_descriptor_library/file_descriptor_info.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"

// NOLINT(namespace-envoy)
namespace cc_proto_descriptor_library {

struct TextFormatTranscoder::InternalData {
  google::protobuf::DescriptorPool descriptor_pool;
  google::protobuf::DynamicMessageFactory message_factory;
};

TextFormatTranscoder::TextFormatTranscoder(bool allow_global_fallback /*= true*/)
    : internals_(std::make_unique<InternalData>()), allow_global_fallback_(allow_global_fallback) {}

// Needs to be defined here so std::unique_ptr doesn't complain about
// TextToBinaryProtoReserializerInternals being incomplete in it's
// destructor.
TextFormatTranscoder::~TextFormatTranscoder() = default;

bool TextFormatTranscoder::parseInto(absl::string_view text_format,
                                     google::protobuf::MessageLite* msg,
                                     google::protobuf::io::ErrorCollector* error_collector) const {
  google::protobuf::io::ArrayInputStream input(text_format.data(), text_format.size());
  return parseInto(&input, msg, error_collector);
}

bool TextFormatTranscoder::parseInto(google::protobuf::io::ZeroCopyInputStream* input_stream,
                                     google::protobuf::MessageLite* msg,
                                     google::protobuf::io::ErrorCollector* error_collector) const {
  std::string serialization;

  if (!toBinarySerializationInternal(msg->GetTypeName(), input_stream, &serialization,
                                     error_collector)) {
    return false;
  }

  return msg->ParseFromString(serialization);
}

void TextFormatTranscoder::loadFileDescriptors(
    const internal::FileDescriptorInfo& file_descriptor_info) {
  if (internals_->descriptor_pool.FindFileByName(std::string(file_descriptor_info.file_name))) {
    return;
  }

  for (int i = 0; file_descriptor_info.deps[i] != nullptr; ++i) {
    loadFileDescriptors(*file_descriptor_info.deps[i]);
  }

  google::protobuf::FileDescriptorProto file_descriptor_proto;
  std::string file_descriptor_bytes;
  absl::Base64Unescape(file_descriptor_info.file_descriptor_bytes_base64, &file_descriptor_bytes);
  file_descriptor_proto.ParseFromString(file_descriptor_bytes);
  internals_->descriptor_pool.BuildFile(file_descriptor_proto);
}

bool TextFormatTranscoder::toBinarySerializationInternal(
    absl::string_view type_name, google::protobuf::io::ZeroCopyInputStream* input_stream,
    std::string* binary_serializtion, google::protobuf::io::ErrorCollector* error_collector) const {
  auto dynamic_message = createEmptyDynamicMessage(type_name, error_collector);
  if (!dynamic_message) {
    // createEmptyDynamicMessage already would have written to the
    // error_collector.
    return false;
  }

  // TextFormat defaults to using the DescriptorPool of the passed in
  // Message*. So we don't have to do anything special with TextFormat::Finder
  // for it to use our DescriptorPool.
  google::protobuf::TextFormat::Parser parser;
  parser.RecordErrorsTo(error_collector);
  if (!parser.Parse(input_stream, dynamic_message.get())) {
    return false;
  }

  return dynamic_message->SerializePartialToString(binary_serializtion);
}

std::unique_ptr<google::protobuf::Message> TextFormatTranscoder::createEmptyDynamicMessage(
    absl::string_view type_name, google::protobuf::io::ErrorCollector* error_collector) const {
  const google::protobuf::Descriptor* descriptor =
      internals_->descriptor_pool.FindMessageTypeByName(std::string(type_name));
  // If you're built with the full runtime then embedding the descriptors and
  // loading them would be information duplicated by the global descriptor
  // pool which hurts builds like superroot that are near all the blaze/forge
  // size limits. Teams that care about not silently falling into this fallback
  // case should make sure their tests are also run in mobile configurations or
  // set allow_global_fallback to false in TextFormatTranscoder's constructor.
  if (allow_global_fallback_ && (descriptor == nullptr)) {
    const auto generated_pool = google::protobuf::DescriptorPool::generated_pool();
    if (generated_pool != nullptr) {
      descriptor = generated_pool->FindMessageTypeByName(std::string(type_name));
    }
  }
  if (descriptor == nullptr) {
    if (error_collector) {
      error_collector->RecordError(0, 0,
                                   absl::StrFormat("Could not find descriptor for: %s", type_name));
    }
    return nullptr;
  }

  auto dynamic_message =
      absl::WrapUnique(internals_->message_factory.GetPrototype(descriptor)->New());
  if (!dynamic_message) {
    if (error_collector) {
      error_collector->RecordError(
          0, 0, absl::StrFormat("Could not create dynamic message for: %s", type_name));
    }
    return nullptr;
  }
  return dynamic_message;
}

} // namespace cc_proto_descriptor_library
