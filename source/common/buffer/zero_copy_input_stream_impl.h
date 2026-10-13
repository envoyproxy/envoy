#pragma once

#include <cstdint>
#include <string>

#include "envoy/buffer/buffer.h"

#include "source/common/protobuf/protobuf.h"

namespace Envoy {

namespace Buffer {

class ZeroCopyInputStreamImpl : public virtual Protobuf::io::ZeroCopyInputStream {
public:
  // Create input stream with one buffer, and finish immediately
  ZeroCopyInputStreamImpl(Buffer::InstancePtr&& buffer);

  // Create input stream with empty buffer
  ZeroCopyInputStreamImpl();

  // Add a buffer to input stream, will consume all buffer from parameter
  // if the stream is not finished
  void move(Buffer::Instance& instance);

  // Mark the stream is finished
  void finish() { finished_ = true; }

  // Protobuf::io::ZeroCopyInputStream
  // See
  // https://developers.google.com/protocol-buffers/docs/reference/cpp/google.protobuf.io.zero_copy_stream#ZeroCopyInputStream
  // for each method details.

  // Note Next() will return true with no data until more data is available if the stream is not
  // finished. It is the caller's responsibility to finish the stream or wrap with
  // LimitingInputStream before passing to protobuf code to avoid a spin loop.
  bool Next(const void** data, int* size) override;
  void BackUp(int count) override;
  bool Skip(int count) override;
  ProtobufTypes::Int64 ByteCount() const override { return byte_count_; }

protected:
  // The last slice is kept to support limited BackUp() calls.
  // This function will drain it.
  void drainLastSlice();

  Buffer::InstancePtr buffer_;
  uint64_t position_{0};
  bool finished_{false};

private:
  uint64_t byte_count_{0};
};

} // namespace Buffer
} // namespace Envoy
