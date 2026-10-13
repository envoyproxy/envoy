#pragma once

#include "source/common/buffer/zero_copy_input_stream_impl.h"

#include "grpc_transcoding/transcoder_input_stream.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

class TranscoderInputStreamImpl : public Buffer::ZeroCopyInputStreamImpl,
                                  public google::grpc::transcoding::TranscoderInputStream {
public:
  // TranscoderInputStream
  int64_t BytesAvailable() const override;
  bool Finished() const override;

  // Returns the total number of bytes stored in the underlying buffer.
  // Useful for flow control.
  uint64_t bytesStored() const;
};

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
