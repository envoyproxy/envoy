#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"

#include "extensions/filters/http/well_known_names.h"

#include "test/extensions/filters/http/common/fuzz/uber_filter.h"
#include "test/proto/bookstore.pb.h"

// This file contains any filter-specific input clean-up needed in the generic filter fuzz target.

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace {

void addFileDescriptorsRecursively(const Protobuf::FileDescriptor* descriptor,
                                   Protobuf::FileDescriptorSet* set,
                                   absl::flat_hash_set<absl::string_view>* added_descriptors) {
  if (!added_descriptors->insert(descriptor->name()).second) {
    // Already added.
    return;
  }
  for (int i = 0; i < descriptor->dependency_count(); i++) {
    addFileDescriptorsRecursively(descriptor->dependency(i), set, added_descriptors);
  }
  descriptor->CopyTo(set->add_file());
}

void addProtoDescriptor(Protobuf::Message* message) {
  envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder& config =
      dynamic_cast<envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&>(
          *message);
  config.clear_services();
  config.add_services("bookstore.Bookstore");

  Protobuf::FileDescriptorSet descriptor_set;
  const auto* file_descriptor =
      Protobuf::DescriptorPool::generated_pool()->FindFileByName("test/proto/bookstore.proto");
  ASSERT(file_descriptor != nullptr);
  // Create a set to keep track of descriptors as they are added.
  absl::flat_hash_set<absl::string_view> added_descriptors;
  addFileDescriptorsRecursively(file_descriptor, &descriptor_set, &added_descriptors);
  descriptor_set.SerializeToString(config.mutable_proto_descriptor_bin());
}
} // namespace

void UberFilterFuzzer::cleanFuzzedConfig(absl::string_view filter_name,
                                         Protobuf::Message* message) {
  // Map filter name to clean-up function.
  ENVOY_LOG_MISC(info, "filter_name {}", filter_name);
  if (filter_name == HttpFilterNames::get().GrpcJsonTranscoder) {
    addProtoDescriptor(message);
  }
}

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
