#include "source/extensions/filters/http/grpc_json_transcoder/transcoder_input_stream_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

int64_t TranscoderInputStreamImpl::BytesAvailable() const { return buffer_->length() - position_; }

bool TranscoderInputStreamImpl::Finished() const { return finished_; }

uint64_t TranscoderInputStreamImpl::bytesStored() const { return buffer_->length(); }

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
