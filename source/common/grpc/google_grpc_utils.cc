#include "common/grpc/google_grpc_utils.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/macros.h"
#include "common/common/stack_array.h"
#include "common/common/utility.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Grpc {

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
  Buffer::RawSlice on_raw_slice;
  // NB: we need to pass in >= 1 in order to get the real "n" (see Buffer::Instance for details).
  const int n_slices = buffer_instance->getRawSlices(&on_raw_slice, 1);
  if (n_slices <= 0) {
    return {};
  }
  auto* container = new BufferInstanceContainer{n_slices, std::move(buffer_instance)};
  if (n_slices == 1) {
    grpc::Slice one_slice(on_raw_slice.mem_, on_raw_slice.len_,
                          &BufferInstanceContainer::derefBufferInstanceContainer, container);
    return {&one_slice, 1};
  }
  STACK_ARRAY(many_raw_slices, Buffer::RawSlice, n_slices);
  container->buffer_->getRawSlices(many_raw_slices.begin(), n_slices);
  std::vector<grpc::Slice> slices;
  slices.reserve(n_slices);
  for (int i = 0; i < n_slices; i++) {
    slices.emplace_back(many_raw_slices[i].mem_, many_raw_slices[i].len_,
                        &BufferInstanceContainer::derefBufferInstanceContainer, container);
  }
  return {&slices[0], slices.size()};
}

struct ByteBufferContainer {
  ByteBufferContainer(int ref_count) : ref_count_(ref_count) {}
  ~ByteBufferContainer() { ::free(fragments_); }
  uint32_t ref_count_;
  Buffer::BufferFragmentImpl* fragments_ = nullptr;
  std::vector<grpc::Slice> slices_;
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
  auto* container = new ByteBufferContainer(static_cast<int>(slices.size()));
  std::function<void(const void*, size_t, const Buffer::BufferFragmentImpl*)> releaser =
      [container](const void*, size_t, const Buffer::BufferFragmentImpl*) {
        container->ref_count_--;
        if (container->ref_count_ <= 0) {
          delete container;
        }
      };
  // NB: addBufferFragment takes a pointer alias to the BufferFragmentImpl which is passed in so we
  // need to ensure that the lifetime of those objects exceeds that of the Buffer::Instance.
  RELEASE_ASSERT(!::posix_memalign(reinterpret_cast<void**>(&container->fragments_),
                                   alignof(Buffer::BufferFragmentImpl),
                                   sizeof(Buffer::BufferFragmentImpl) * slices.size()),
                 "posix_memalign failure");
  for (size_t i = 0; i < slices.size(); i++) {
    new (&container->fragments_[i])
        Buffer::BufferFragmentImpl(slices[i].begin(), slices[i].size(), releaser);
  }
  for (size_t i = 0; i < slices.size(); i++) {
    buffer->addBufferFragment(container->fragments_[i]);
  }
  container->slices_ = std::move(slices);
  return buffer;
}

} // namespace Grpc
} // namespace Envoy
