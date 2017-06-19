#include "common/grpc/transcoder_input_stream_impl.h"

namespace Envoy {
namespace Grpc {

int64_t TranscoderInputStreamImpl::BytesAvailable() const { return buffer_->length() - position_; }

} // namespace Grpc
} // namespace Envoy
