#include "bazel/cc_proto_descriptor_library/file_descriptor_info.h"

#include "absl/log/absl_log.h"
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
  if (!absl::Base64Unescape(descriptor_info.file_descriptor_bytes_base64, &file_descriptor_bytes) ||
      !file_descriptor_proto.ParseFromString(file_descriptor_bytes)) {
    // Embedded descriptors are generated at build time, so a failure here is a build bug, not a
    // runtime condition. Fail loudly in debug builds, and skip rather than register garbage in
    // release builds.
    ABSL_LOG(DFATAL) << "Failed to parse embedded file descriptor for: "
                     << descriptor_info.file_name;
    return;
  }
  if (descriptor_pool->BuildFile(file_descriptor_proto) == nullptr) {
    ABSL_LOG(DFATAL) << "Failed to build embedded file descriptor for: "
                     << descriptor_info.file_name;
  }
}

} // namespace internal
} // namespace cc_proto_descriptor_library
