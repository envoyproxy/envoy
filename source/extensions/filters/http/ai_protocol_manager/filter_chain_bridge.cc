#include "source/extensions/filters/http/ai_protocol_manager/filter_chain_bridge.h"

#include "envoy/http/codes.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

void DecoderFilterChainBridge::onUnrecoverableError() {
  stats_.request_external_buffer_error_.inc();
  callbacks_.sendLocalReply(Http::Code::InternalServerError, "AI protocol buffer error", nullptr,
                            std::nullopt, "ai_protocol_manager_external_buffer_error");
}

void EncoderFilterChainBridge::onUnrecoverableError() {
  stats_.response_external_buffer_error_.inc();
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
