#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"

#include "extensions/filters/http/well_known_names.h"

#include "test/extensions/filters/http/common/fuzz/uber_filter.h"
#include "test/proto/bookstore.pb.h"

// This file contains any filter-specific setup and input clean-up needed in the generic filter fuzz
// target.

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace {

void addFileDescriptorsRecursively(const Protobuf::FileDescriptor& descriptor,
                                   Protobuf::FileDescriptorSet& set,
                                   absl::flat_hash_set<absl::string_view>& added_descriptors) {
  if (!added_descriptors.insert(descriptor.name()).second) {
    // Already added.
    return;
  }
  for (int i = 0; i < descriptor.dependency_count(); i++) {
    addFileDescriptorsRecursively(*descriptor.dependency(i), set, added_descriptors);
  }
  descriptor.CopyTo(set.add_file());
}

void addBookstoreProtoDescriptor(Protobuf::Message* message) {
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
  addFileDescriptorsRecursively(*file_descriptor, descriptor_set, added_descriptors);
  descriptor_set.SerializeToString(config.mutable_proto_descriptor_bin());
}
} // namespace

void UberFilterFuzzer::cleanFuzzedConfig(absl::string_view filter_name,
                                         Protobuf::Message* message) {
  // Map filter name to clean-up function.
  if (filter_name == HttpFilterNames::get().GrpcJsonTranscoder) {
    addBookstoreProtoDescriptor(message);
  }
}

void UberFilterFuzzer::perFilterSetup() {
  // Prepare expectations for the ext_authz filter.
  addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
  ON_CALL(connection_, remoteAddress()).WillByDefault(testing::ReturnRef(addr_));
  ON_CALL(connection_, localAddress()).WillByDefault(testing::ReturnRef(addr_));
  ON_CALL(callbacks_, connection()).WillByDefault(testing::Return(&connection_));
  ON_CALL(callbacks_, activeSpan())
      .WillByDefault(testing::ReturnRef(Tracing::NullSpan::instance()));
  callbacks_.stream_info_.protocol_ = Envoy::Http::Protocol::Http2;

  // Prepare expectations for dynamic forward proxy.
  ON_CALL(factory_context_.dispatcher_, createDnsResolver(_, _))
      .WillByDefault(testing::Return(resolver_));

  // Prepare expectations for TAP config.
  ON_CALL(factory_context_, admin()).WillByDefault(testing::ReturnRef(factory_context_.admin_));
  ON_CALL(factory_context_.admin_, addHandler(_, _, _, _, _)).WillByDefault(testing::Return(true));
  ON_CALL(factory_context_.admin_, removeHandler(_)).WillByDefault(testing::Return(true));
}

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
