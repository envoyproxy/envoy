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

std::shared_ptr<grpc::Channel>
GoogleGrpcUtils::createChannel(const envoy::config::core::v3::GrpcService& config, Api::Api& api) {
  std::shared_ptr<grpc::ChannelCredentials> creds = getGoogleGrpcChannelCredentials(config, api);
  return CreateChannel(config.google_grpc().target_uri(), creds);
}

} // namespace Grpc
} // namespace Envoy
