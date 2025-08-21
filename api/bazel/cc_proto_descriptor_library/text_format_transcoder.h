#pragma once

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "bazel/cc_proto_descriptor_library/create_dynamic_message.h"
#include "google/protobuf/io/tokenizer.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "google/protobuf/message.h"

// NOLINT(namespace-envoy)
namespace cc_proto_descriptor_library {
namespace internal {

struct FileDescriptorInfo;

} // namespace internal

// Takes a text formatted proto and translates it to a MessageLite proto so it
// can be used by the lite runtime. Requires the that text formatted protos and
// all of its extensions are registered with this object.
class TextFormatTranscoder final {
public:
  explicit TextFormatTranscoder(
      // Whether to allow using the global descriptor pool as a fallback. See
      // text_format_transcoder.cc for more information.
      bool allow_global_fallback = true);
  ~TextFormatTranscoder();

  bool parseInto(absl::string_view text_format, google::protobuf::MessageLite* msg,
                 google::protobuf::io::ErrorCollector* error_collector = nullptr) const;

  bool parseInto(google::protobuf::io::ZeroCopyInputStream* input_stream,
                 google::protobuf::MessageLite* msg,
                 google::protobuf::io::ErrorCollector* error_collector = nullptr) const;

  void loadFileDescriptors(const internal::FileDescriptorInfo& file_descriptor_info);

private:
  // This function needs the member function CreateEmptyDynamicMessage. The
  // target the defines the function has its own visibility whitelist which
  // requires a dance with these declarations.
  friend std::unique_ptr<google::protobuf::Message>
  createDynamicMessage(const TextFormatTranscoder& transcoder,
                       const google::protobuf::MessageLite& message,
                       google::protobuf::io::ErrorCollector* error_collector);

  std::unique_ptr<google::protobuf::Message>
  createEmptyDynamicMessage(absl::string_view type_name,
                            google::protobuf::io::ErrorCollector* error_collector) const;

  bool toBinarySerializationInternal(absl::string_view type_name,
                                     google::protobuf::io::ZeroCopyInputStream* input_stream,
                                     std::string* binary_serializtion,
                                     google::protobuf::io::ErrorCollector* error_collector) const;

  struct InternalData;

  std::unique_ptr<InternalData> internals_;
  const bool allow_global_fallback_;
};

} // namespace cc_proto_descriptor_library
