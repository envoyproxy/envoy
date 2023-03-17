#include "envoy/extensions/filters/http/file_system_buffer/v3/file_system_buffer.pb.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/extensions/filters/http/tap/v3/tap.pb.h"

#include "source/common/tracing/http_tracer_impl.h"

#include "test/extensions/filters/http/common/fuzz/uber_filter.h"
#include "test/proto/bookstore.pb.h"
#include "test/test_common/registry.h"

// This file contains any filter-specific setup and input clean-up needed in the generic filter fuzz
// target.

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace {

// Limit the number of threads for the FileSystemBufferFilterConfig.manager_config.thread_count to
// 8 to ensure test stays responsive.
static const uint32_t kMaxAsyncFileManagerThreadCount = 8;

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

void UberFilterFuzzer::guideAnyProtoType(test::fuzz::HttpData* mutable_data, uint choice) {
  // These types are request/response from the test Bookstore service
  // for the gRPC Transcoding filter.
  static const std::vector<std::string> expected_types = {
      "type.googleapis.com/bookstore.ListShelvesResponse",
      "type.googleapis.com/bookstore.CreateShelfRequest",
      "type.googleapis.com/bookstore.GetShelfRequest",
      "type.googleapis.com/bookstore.DeleteShelfRequest",
      "type.googleapis.com/bookstore.ListBooksRequest",
      "type.googleapis.com/bookstore.CreateBookRequest",
      "type.googleapis.com/bookstore.GetBookRequest",
      "type.googleapis.com/bookstore.UpdateBookRequest",
      "type.googleapis.com/bookstore.DeleteBookRequest",
      "type.googleapis.com/bookstore.GetAuthorRequest",
      "type.googleapis.com/bookstore.EchoBodyRequest",
      "type.googleapis.com/bookstore.EchoStructReqResp",
      "type.googleapis.com/bookstore.Shelf",
      "type.googleapis.com/bookstore.Book",
      "type.googleapis.com/google.protobuf.Empty",
      "type.googleapis.com/google.api.HttpBody",
  };
  ProtobufWkt::Any* mutable_any = mutable_data->mutable_proto_body()->mutable_message();
  const std::string& type_url = expected_types[choice % expected_types.size()];
  mutable_any->set_type_url(type_url);
}

void cleanTapConfig(Protobuf::Message* message) {
  envoy::extensions::filters::http::tap::v3::Tap& config =
      dynamic_cast<envoy::extensions::filters::http::tap::v3::Tap&>(*message);
  if (config.common_config().config_type_case() ==
      envoy::extensions::common::tap::v3::CommonExtensionConfig::ConfigTypeCase::kStaticConfig) {
    auto const& output_config = config.common_config().static_config().output_config();
    // TODO(samflattery): remove once StreamingGrpcSink is implemented
    // a static config filter is required to have one sink, but since validation isn't performed on
    // the filter until after this function runs, we have to manually check that there are sinks
    // before checking that they are not StreamingGrpc
    if (!output_config.sinks().empty() &&
        output_config.sinks(0).output_sink_type_case() ==
            envoy::config::tap::v3::OutputSink::OutputSinkTypeCase::kStreamingGrpc) {
      // will be caught in UberFilterFuzzer::fuzz
      throw EnvoyException(
          "received input with not implemented output_sink_type StreamingGrpcSink");
    } else if (!output_config.sinks().empty() &&
               (output_config.sinks(0).output_sink_type_case() ==
                    envoy::config::tap::v3::OutputSink::OutputSinkTypeCase::kBufferedAdmin ||
                output_config.sinks(0).output_sink_type_case() ==
                    envoy::config::tap::v3::OutputSink::OutputSinkTypeCase::kStreamingAdmin)) {
      // will be caught in UberFilterFuzzer::fuzz
      throw EnvoyException("received input for which factory can not create for output_sink_type "
                           "BufferAdmin or StreamingAdmin");
    }
  }
}

void cleanFileSystemBufferConfig(Protobuf::Message* message) {
  envoy::extensions::filters::http::file_system_buffer::v3::FileSystemBufferFilterConfig& config =
      dynamic_cast<
          envoy::extensions::filters::http::file_system_buffer::v3::FileSystemBufferFilterConfig&>(
          *message);
  if (config.manager_config().thread_pool().thread_count() > kMaxAsyncFileManagerThreadCount) {
    throw EnvoyException(fmt::format(
        "received input exceeding the allowed number of threads ({} > {}) for "
        "FileSystemBufferFilter.AsyncFileManager",
        config.manager_config().thread_pool().thread_count(), kMaxAsyncFileManagerThreadCount));
  }
}

void UberFilterFuzzer::cleanFuzzedConfig(absl::string_view filter_name,
                                         Protobuf::Message* message) {
  // Map filter name to clean-up function.
  if (filter_name == "envoy.filters.http.grpc_json_transcoder") {
    // Add a valid service proto descriptor.
    addBookstoreProtoDescriptor(message);
  } else if (filter_name == "envoy.filters.http.tap") {
    // TapDS oneof field and OutputSinkType StreamingGrpc not implemented
    cleanTapConfig(message);
  } else if (filter_name == "envoy.filters.http.file_system_buffer") {
    // Limit the number of threads to create to kMaxAsyncFileManagerThreadCount
    cleanFileSystemBufferConfig(message);
  }
}

void UberFilterFuzzer::perFilterSetup() {
  // Prepare expectations for the ext_authz filter.
  addr_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 1111);
  connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr_);
  connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(addr_);
  ON_CALL(factory_context_, clusterManager()).WillByDefault(testing::ReturnRef(cluster_manager_));
  ON_CALL(cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillByDefault(Return(&async_request_));

  ON_CALL(decoder_callbacks_, connection())
      .WillByDefault(testing::Return(OptRef<const Network::Connection>{connection_}));
  ON_CALL(decoder_callbacks_, activeSpan())
      .WillByDefault(testing::ReturnRef(Tracing::NullSpan::instance()));
  decoder_callbacks_.stream_info_.protocol_ = Envoy::Http::Protocol::Http2;

  ON_CALL(encoder_callbacks_, connection())
      .WillByDefault(testing::Return(OptRef<const Network::Connection>{connection_}));
  ON_CALL(encoder_callbacks_, activeSpan())
      .WillByDefault(testing::ReturnRef(Tracing::NullSpan::instance()));
  encoder_callbacks_.stream_info_.protocol_ = Envoy::Http::Protocol::Http2;

  // Prepare expectations for dynamic forward proxy.
  NiceMock<Network::MockDnsResolverFactory> dns_resolver_factory;
  Registry::InjectFactory<Network::DnsResolverFactory> registered_dns_factory(dns_resolver_factory);
  ON_CALL(dns_resolver_factory, createDnsResolver(_, _, _))
      .WillByDefault(testing::Return(resolver_));

  // Prepare expectations for TAP config.
  ON_CALL(factory_context_, admin())
      .WillByDefault(testing::Return(OptRef<Server::Admin>{factory_context_.admin_}));
  ON_CALL(factory_context_.admin_, addHandler(_, _, _, _, _, _))
      .WillByDefault(testing::Return(true));
  ON_CALL(factory_context_.admin_, removeHandler(_)).WillByDefault(testing::Return(true));

  // Prepare expectations for WASM filter.
  ON_CALL(factory_context_, listenerMetadata())
      .WillByDefault(testing::ReturnRef(listener_metadata_));
  ON_CALL(factory_context_.api_, customStatNamespaces())
      .WillByDefault(testing::ReturnRef(custom_stat_namespaces_));

  // Prepare expectations for AWSRequestSigning filter
  ON_CALL(decoder_callbacks_, addDecodedData(_, _))
      .WillByDefault([this](Buffer::Instance& data, bool) { decoding_buffer_ = &data; });
  ON_CALL(decoder_callbacks_, decodingBuffer()).WillByDefault([this]() -> const Buffer::Instance* {
    return decoding_buffer_;
  });
  ON_CALL(encoder_callbacks_, dispatcher()).WillByDefault([this]() -> Event::Dispatcher& {
    return *worker_thread_dispatcher_;
  });
  ON_CALL(decoder_callbacks_, dispatcher()).WillByDefault([this]() -> Event::Dispatcher& {
    return *worker_thread_dispatcher_;
  });
  ON_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, true))
      .WillByDefault([this]() -> void { finishFilter(encoder_filter_.get()); });
  ON_CALL(encoder_callbacks_, addEncodedData(_, true)).WillByDefault([this]() -> void {
    finishFilter(encoder_filter_.get());
  });
  ON_CALL(encoder_callbacks_, continueEncoding()).WillByDefault([this]() -> void {
    finishFilter(encoder_filter_.get());
  });
  ON_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, true))
      .WillByDefault([this]() -> void { finishFilter(decoder_filter_.get()); });
  ON_CALL(decoder_callbacks_, addDecodedData(_, true)).WillByDefault([this]() -> void {
    finishFilter(decoder_filter_.get());
  });
  ON_CALL(decoder_callbacks_, continueDecoding()).WillByDefault([this]() -> void {
    finishFilter(decoder_filter_.get());
  });
}

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
