#include "bazel/cc_proto_descriptor_library/create_dynamic_message.h"

#include <memory>

#include "absl/memory/memory.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "bazel/cc_proto_descriptor_library/text_format_transcoder.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"

// NOLINT(namespace-envoy)
namespace cc_proto_descriptor_library {

std::unique_ptr<google::protobuf::Message>
createDynamicMessage(const TextFormatTranscoder& transcoder,
                     const google::protobuf::MessageLite& message,
                     google::protobuf::io::ErrorCollector* error_collector /*= nullptr*/) {
  auto dynamic_message =
      transcoder.createEmptyDynamicMessage(message.GetTypeName(), error_collector);

  if (dynamic_message) {
    dynamic_message->ParsePartialFromString(message.SerializePartialAsString());
  }

  return dynamic_message;
}

} // namespace cc_proto_descriptor_library
