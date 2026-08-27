#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

#include "envoy/http/codes.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

void DecoderFilterChainBridge::registerReplayWatermarks(ReplayWatermarkHandler& handler) {
  handler_ = &handler;
  callbacks_.addUpstreamWatermarkCallbacks(*this);
  registered_ = true;
}

void DecoderFilterChainBridge::unregisterReplayWatermarks() {
  if (registered_) {
    callbacks_.removeUpstreamWatermarkCallbacks(*this);
    registered_ = false;
  }
  handler_ = nullptr;
}

void DecoderFilterChainBridge::onUnrecoverableError() {
  callbacks_.sendLocalReply(Http::Code::InternalServerError, "AI protocol buffer error", nullptr,
                            std::nullopt, "ai_protocol_manager_external_buffer_error");
}

void EncoderFilterChainBridge::registerReplayWatermarks(ReplayWatermarkHandler& handler) {
  handler_ = &handler;
  decoder_callbacks_.addDownstreamWatermarkCallbacks(*this);
  registered_ = true;
}

void EncoderFilterChainBridge::unregisterReplayWatermarks() {
  if (registered_) {
    decoder_callbacks_.removeDownstreamWatermarkCallbacks(*this);
    registered_ = false;
  }
  handler_ = nullptr;
}

void EncoderFilterChainBridge::onUnrecoverableError() {
  // On the response path the headers (and possibly some body) may already be in
  // flight. sendLocalReply handles this best-effort: if the response has not
  // started it generates a local reply, otherwise it either ships the reply
  // directly to the downstream codec or resets the stream (see its contract in
  // envoy/http/filter.h). Either way we avoid emitting a truncated payload.
  encoder_callbacks_.sendLocalReply(Http::Code::InternalServerError, "AI protocol buffer error",
                                    nullptr, std::nullopt,
                                    "ai_protocol_manager_external_buffer_error");
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
