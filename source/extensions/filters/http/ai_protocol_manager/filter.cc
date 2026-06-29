#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include <memory>

#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

void AiProtocolManagerFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  PassThroughFilter::setDecoderFilterCallbacks(callbacks);
  // Construct the decode-path manager. Its constructor subscribes to upstream
  // watermarks (via the bridge) so replay can be paced against upstream
  // back-pressure; subscribing may immediately deliver high-watermark callbacks
  // if the upstream is already backed up.
  decode_manager_ = std::make_unique<BufferManager>(
      buffer_factory_, std::make_unique<DecoderFilterChainBridge>(*decoder_callbacks_));
}

void AiProtocolManagerFilter::onDestroy() {
  if (decode_manager_ != nullptr) {
    decode_manager_->onDestroy();
    decode_manager_.reset();
  }
}

Http::FilterDataStatus AiProtocolManagerFilter::decodeData(Buffer::Instance& data,
                                                           bool end_stream) {
  return decode_manager_->onData(data, end_stream);
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
