#include "extensions/quic_listeners/quiche/platform/quic_mem_slice_span_impl.h"
#include "quiche/quic/platform/api/quic_mem_slice.h"
#include "quiche/quic/core/frames/quic_message_frame.h"
#include "common/buffer/buffer_impl.h"

namespace quic {

QuicByteCount QuicMemSliceSpanImpl::SaveMemSlicesInSendBuffer(QuicStreamSendBuffer* send_buffer) {
  uint64_t num_slices = buffer_.getRawSlices(nullptr, 0);
  ASSERT(num_slices > index);
  STACK_ARRAY(slices, RawSlice, num_slices);
  buffer_.getRawSlices(slices.begin(), num_slices);
  size_t saved_length = 0;
  for (auto slice : slices) {
    if (slice.len_ == 0) {
      continue;
    }
    auto single_slice_buffer = std::make_shared<Envoy::Buffer::OwnedImpl>();
    // Move each slice into a stand-alone buffer.
    // TODO(danzh): investigate the cost of allocating one buffer per slice.
    // If it turns out to be expensive, add a new function to free data in the middle in buffer
    // interface and re-design QuicMemSliceImpl.
    single_slice_buffer.move(buffer_, slice.len_);
    send_buffer->SaveMemSlice(QuicMemSlice(QuicMemSliceImpl(std::move(single_slice_buffer))));
    saved_length += slice.len_;
  }
  return saved_length;
}

void QuicMemSliceSpanImpl::SaveMemSlicesAsMessageData(QuicMessageFrame* message_frame) {
  uint64_t num_slices = buffer_.getRawSlices(nullptr, 0);
  ASSERT(num_slices > index);
  STACK_ARRAY(slices, RawSlice, num_slices);
  buffer_.getRawSlices(slices.begin(), num_slices);
  for (auto slice : slices) {
    if (slice.len_ == 0) {
      continue;
    }
    message_frame->message_length += slice.len_;
    auto single_slice_buffer = std::make_shared<Envoy::Buffer::OwnedImpl>();
    single_slice_buffer.move(buffer_, slice.len_);
    message_frame->message_data.push_back(
        QuicMemSlice(QuicMemSliceImpl(std::move(single_slice_buffer))));
  }
}

} // namespace quic
