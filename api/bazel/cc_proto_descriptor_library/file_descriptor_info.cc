#include "bazel/cc_proto_descriptor_library/file_descriptor_info.h"

#include "absl/log/absl_check.h"
#include "absl/strings/escaping.h"
#include "google/protobuf/descriptor.h"
#include "net/proto2/proto/descriptor.proto.h"

// NOLINT(namespace-envoy)
namespace cc_proto_descriptor_library {
namespace internal {

void loadFileDescriptors(const FileDescriptorInfo& descriptor_info,
                         google::protobuf::DescriptorPool* descriptor_pool) {
  if (descriptor_pool->FindFileByName(descriptor_info.file_name)) {
    return;
  }

  for (int i = 0; descriptor_info.deps[i] != nullptr; ++i) {
    loadFileDescriptors(*descriptor_info.deps[i], descriptor_pool);
  }

  google::protobuf::FileDescriptorProto file_descriptor_proto;
  std::string file_descriptor_bytes;
  ABSL_CHECK(
      absl::Base64Unescape(descriptor_info.file_descriptor_bytes_base64, &file_descriptor_bytes))
      << "Failed to base64-decode embedded descriptor for " << descriptor_info.file_name;
  ABSL_CHECK(file_descriptor_proto.ParseFromString(file_descriptor_bytes))
      << "Failed to parse embedded descriptor for " << descriptor_info.file_name;
  descriptor_pool->BuildFile(file_descriptor_proto);
}

} // namespace internal
} // namespace cc_proto_descriptor_library
