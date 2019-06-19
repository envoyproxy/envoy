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

  for (size_t i = 0; i < slices.size(); i++) {
    buffer->addBufferFragment(*new GrpcSliceBufferFragmentImpl(std::move(slices[i])));
  }
  return buffer;
}

} // namespace Grpc
} // namespace Envoy
