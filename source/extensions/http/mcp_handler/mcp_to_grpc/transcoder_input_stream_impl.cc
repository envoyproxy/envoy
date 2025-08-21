#include "source/extensions/http/mcp_handler/mcp_to_grpc/transcoder_input_stream_impl.h"
namespace Envoy {
namespace Extensions {
namespace Http {
namespace McpHandler {
namespace McpToGrpc {

int64_t TranscoderInputStreamImpl::BytesAvailable() const { return buffer_->length() - position_; }

bool TranscoderInputStreamImpl::Finished() const { return finished_; }

uint64_t TranscoderInputStreamImpl::bytesStored() const { return buffer_->length(); }

} // namespace McpToGrpc
} // namespace McpHandler
} // namespace Http
} // namespace Extensions
} // namespace Envoy
