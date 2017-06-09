#pragma once

#include "common/buffer/buffer_impl.h"

#include "src/transcoder_input_stream.h"

namespace Envoy {
namespace Grpc {

class TranscoderInputStreamImpl : public google::grpc::transcoding::TranscoderInputStream {
public:
  // Add a buffer to input stream, will consume all buffer from parameter
  // if the stream is not finished
  void Move(Buffer::Instance& instance);

  // Mark the buffer is finished
  void Finish() { finished_ = true; }

  // TranscoderInputStreamImpl
  virtual bool Next(const void** data, int* size) override;
  virtual void BackUp(int count) override;
  virtual bool Skip(int count) override; // Not implemented
  virtual google::protobuf::int64 ByteCount() const override { return byte_count_; }
  virtual int64_t BytesAvailable() const override;

private:
  Buffer::OwnedImpl buffer_;
  int position_{0};
  int64_t byte_count_{0};
  bool finished_{false};
};

} // namespace Grpc
} // namespace Envoy
