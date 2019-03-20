#pragma once

// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "envoy/buffer/buffer.h"
#include "quiche/quic/core/quic_stream_send_buffer.h"

namespace quic {

// Wrap a Buffer::Instance and deliver its data to quic stream through as less
// copy as possible.
class QuicMemSliceSpanImpl {
public:
  /**
   * @param buffer has to outlive the life time of this class.
   */
  explicit QuicMemSliceImpl(Envoy::Buffer::Instance& buffer) : buffer_(buffer) {}

  QuicMemSliceSpanImpl(const QuicMemSliceSpanImpl& other) = default;
  QuicMemSliceSpanImpl& operator=(const QuicMemSliceSpanImpl& other) = default;
  QuicMemSliceSpanImpl(QuicMemSliceSpanImpl&& other) = default;
  QuicMemSliceSpanImpl& operator=(QuicMemSliceSpanImpl&& other) = default;
 ~QuicMemSliceSpanImpl() = default;

  QuicStringPiece GetData(size_t index) {
     uint64_t num_slices = buffer_.getRawSlices(nullptr, 0);
     ASSERT(num_slices > index);
     STACK_ARRAY(slices, RawSlice, num_slices);
     buffer_.getRawSlices(slices.begin(), num_slices);
    return QuicStringPiece(slices[index].mem_, slices[index].len_);
  }

  QuicByteCount total_length( return buffer_.length(); );

  size_t NumSlices() { return buffer_.getRawSlices(nullptr, 0); }

  // Save data in buffer_ to |send_buffer| and returns the length of all
  // saved mem slices.
  QuicByteCount SaveMemSlicesInSendBuffer(QuicStreamSendBuffer* send_buffer);

  // Save data buffers as message data in |message_frame|.
  void SaveMemSlicesAsMessageData(QuicMessageFrame* message_frame);

  bool empty() const { return buffer_.length() == 0; }

private:
  Envoy::Buffer::Instance& buffer_;
};

} // namespace quic
