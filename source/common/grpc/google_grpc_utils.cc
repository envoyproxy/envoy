#include "common/grpc/google_grpc_utils.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "envoy/grpc/google_grpc_creds.h"
#include "envoy/registry/registry.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/macros.h"
#include "common/common/utility.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/match.h"

namespace Envoy {
namespace Grpc {

namespace {

std::shared_ptr<grpc::ChannelCredentials>
getGoogleGrpcChannelCredentials(const envoy::config::core::v3::GrpcService& grpc_service,
                                Api::Api& api) {
  GoogleGrpcCredentialsFactory* credentials_factory = nullptr;
  const std::string& google_grpc_credentials_factory_name =
      grpc_service.google_grpc().credentials_factory_name();
  if (google_grpc_credentials_factory_name.empty()) {
    credentials_factory = Registry::FactoryRegistry<GoogleGrpcCredentialsFactory>::getFactory(
        "envoy.grpc_credentials.default");
  } else {
    credentials_factory = Registry::FactoryRegistry<GoogleGrpcCredentialsFactory>::getFactory(
        google_grpc_credentials_factory_name);
  }
  if (credentials_factory == nullptr) {
    throw EnvoyException(absl::StrCat("Unknown google grpc credentials factory: ",
                                      google_grpc_credentials_factory_name));
  }
  return credentials_factory->getChannelCredentials(grpc_service, api);
}

// InitialMetadataInterceptor is used to inject the given initial metadata to gRPC channel.
class InitialMetadataInterceptor : public grpc::experimental::Interceptor {
public:
  InitialMetadataInterceptor(
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue>& initial_metadata)
      : initial_metadata_(initial_metadata) {}

  void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override {
    if (methods->QueryInterceptionHookPoint(
            grpc::experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
      auto* metadata_map = methods->GetSendInitialMetadata();
      if (metadata_map != nullptr) {
        for (const auto& header_value : initial_metadata_) {
          metadata_map->insert(std::make_pair(header_value.key(), header_value.value()));
        }
      }
    }
    methods->Proceed();
  }

private:
  const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue>& initial_metadata_;
};

class InitialMetadataInterceptorFactory
    : public grpc::experimental::ClientInterceptorFactoryInterface {
public:
  InitialMetadataInterceptorFactory(
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue>& initial_metadata)
      : initial_metadata_(initial_metadata) {}

  grpc::experimental::Interceptor*
  CreateClientInterceptor(grpc::experimental::ClientRpcInfo*) override {
    return new InitialMetadataInterceptor(initial_metadata_);
  }

private:
  const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue> initial_metadata_;
};

} // namespace

struct BufferInstanceContainer {
  BufferInstanceContainer(int ref_count, Buffer::InstancePtr&& buffer)
      : ref_count_(ref_count), buffer_(std::move(buffer)) {}
  std::atomic<uint32_t> ref_count_; // In case gPRC dereferences in a different threads.
  Buffer::InstancePtr buffer_;

  static void derefBufferInstanceContainer(void* container_ptr) {
    auto container = static_cast<BufferInstanceContainer*>(container_ptr);
    container->ref_count_--;
    // This is safe because the ref_count_ is never incremented.
    if (container->ref_count_ <= 0) {
      delete container;
    }
  }
};

grpc::ByteBuffer GoogleGrpcUtils::makeByteBuffer(Buffer::InstancePtr&& buffer_instance) {
  if (!buffer_instance) {
    return {};
  }
  Buffer::RawSliceVector raw_slices = buffer_instance->getRawSlices();
  if (raw_slices.empty()) {
    return {};
  }

  auto* container =
      new BufferInstanceContainer{static_cast<int>(raw_slices.size()), std::move(buffer_instance)};
  std::vector<grpc::Slice> slices;
  slices.reserve(raw_slices.size());
  for (Buffer::RawSlice& raw_slice : raw_slices) {
    slices.emplace_back(raw_slice.mem_, raw_slice.len_,
                        &BufferInstanceContainer::derefBufferInstanceContainer, container);
  }
  return {&slices[0], slices.size()};
}

class GrpcSliceBufferFragmentImpl : public Buffer::BufferFragment {
public:
  explicit GrpcSliceBufferFragmentImpl(grpc::Slice&& slice) : slice_(std::move(slice)) {}

  // Buffer::BufferFragment
  const void* data() const override { return slice_.begin(); }
  size_t size() const override { return slice_.size(); }
  void done() override { delete this; }

private:
  const grpc::Slice slice_;
};

Buffer::InstancePtr GoogleGrpcUtils::makeBufferInstance(const grpc::ByteBuffer& byte_buffer) {
  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  if (byte_buffer.Length() == 0) {
    return buffer;
  }
  // NB: ByteBuffer::Dump moves the data out of the ByteBuffer so we need to ensure that the
  // lifetime of the Slice(s) exceeds our Buffer::Instance.
  std::vector<grpc::Slice> slices;
  if (!byte_buffer.Dump(&slices).ok()) {
    return nullptr;
  }

  for (auto& slice : slices) {
    buffer->addBufferFragment(*new GrpcSliceBufferFragmentImpl(std::move(slice)));
  }
  return buffer;
}

grpc::ChannelArguments
GoogleGrpcUtils::channelArgsFromConfig(const envoy::config::core::v3::GrpcService& config) {
  grpc::ChannelArguments args;
  for (const auto& channel_arg : config.google_grpc().channel_args().args()) {
    switch (channel_arg.second.value_specifier_case()) {
    case envoy::config::core::v3::GrpcService::GoogleGrpc::ChannelArgs::Value::kStringValue: {
      args.SetString(channel_arg.first, channel_arg.second.string_value());
      break;
    }
    case envoy::config::core::v3::GrpcService::GoogleGrpc::ChannelArgs::Value::kIntValue: {
      args.SetInt(channel_arg.first, channel_arg.second.int_value());
      break;
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
  return args;
}

std::shared_ptr<grpc::Channel>
GoogleGrpcUtils::createChannel(const envoy::config::core::v3::GrpcService& config, Api::Api& api,
                               bool enable_initial_metadata_interceptor) {
  std::shared_ptr<grpc::ChannelCredentials> creds = getGoogleGrpcChannelCredentials(config, api);
  const grpc::ChannelArguments args = channelArgsFromConfig(config);
  if (!enable_initial_metadata_interceptor || config.initial_metadata().empty()) {
    // Skip adding initial metadata interceptor if it is not enabled, or initial metadata is not
    // configured.
    return CreateCustomChannel(config.google_grpc().target_uri(), creds, args);
  }

  // Create gRPC channel with initial metadata interceptor.
  std::vector<std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>>
      interceptor_factories;
  interceptor_factories.push_back(
      std::make_unique<InitialMetadataInterceptorFactory>((config.initial_metadata())));
  return ::grpc::experimental::CreateCustomChannelWithInterceptors(
      config.google_grpc().target_uri(), creds, args, std::move(interceptor_factories));
}

} // namespace Grpc
} // namespace Envoy
