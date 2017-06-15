#pragma once

#include "common/buffer/zero_copy_input_stream_impl.h"

#include "grpc_transcoding/transcoder_input_stream.h"

namespace Envoy {
namespace Grpc {

class TranscoderInputStreamImpl : public Buffer::ZeroCopyInputStreamImpl,
                                  public google::grpc::transcoding::TranscoderInputStream {
public:
  // TranscoderInputStream
  virtual int64_t BytesAvailable() const override;
};

} // namespace Grpc
} // namespace Envoy
