#include "contrib/ws_local_ratelimit/filters/http/source/ws_local_ratelimit_filter.h"

#include <optional>

#include "envoy/common/exception.h"
#include "envoy/stats/stats.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/macros.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace WsLocalRateLimitFilter {

WsLocalRateLimitConfig::WsLocalRateLimitConfig(
    const envoy::extensions::filters::http::ws_local_ratelimit::v3alpha::WsLocalRateLimit& config,
    Stats::Scope& scope)
    : fill_interval_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(config.token_bucket(), fill_interval, 0))),
      max_tokens_(config.token_bucket().max_tokens()),
      tokens_per_fill_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.token_bucket(), tokens_per_fill, 1)),
      rejection_message_(config.rejection_message()),
      encoded_rejection_frame_(encodeRejectionFrame(rejection_message_)),
      stats_(generateStats(config.stat_prefix(), scope)) {
  // Mirror the validation performed by LocalRateLimiterImpl so that an invalid fill interval is
  // rejected at config-load time instead of throwing the first time a per-connection rate
  // limiter is lazily constructed on the request path.
  if (fill_interval_.count() > 0 && max_tokens_ != 0 &&
      fill_interval_ < std::chrono::milliseconds(50)) {
    throw EnvoyException("local rate limit token bucket fill timer must be >= 50ms");
  }
}

WsLocalRateLimitStats WsLocalRateLimitConfig::generateStats(const std::string& prefix,
                                                            Stats::Scope& scope) {
  const std::string final_prefix = prefix + ".ws_local_rate_limit";
  return {ALL_WS_LOCAL_RATE_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

std::optional<std::string>
WsLocalRateLimitConfig::encodeRejectionFrame(const std::string& rejection_message) {
  if (rejection_message.empty()) {
    return std::nullopt;
  }

  WebSocket::Frame frame;
  frame.final_fragment_ = true;
  frame.opcode_ = WebSocket::kFrameOpcodeText;
  // Frames sent from the server to the client must not be masked.
  frame.masking_key_ = std::nullopt;
  frame.payload_length_ = rejection_message.size();

  WebSocket::Encoder encoder;
  std::optional<std::vector<uint8_t>> header = encoder.encodeFrameHeader(frame);
  if (!header.has_value()) {
    // Can't happen in practice (kFrameOpcodeText is always a valid opcode).
    return std::nullopt;
  }

  std::string encoded(header->begin(), header->end());
  encoded += rejection_message;
  return encoded;
}

const std::string& PerStreamRateLimiter::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "ws_local_ratelimit.per_stream_rate_limiter");
}

Http::FilterHeadersStatus WsLocalRateLimitFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                bool) {
  is_websocket_ = Http::Utility::isWebSocketUpgradeRequest(headers);
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus WsLocalRateLimitFilter::decodeData(Buffer::Instance& data, bool) {
  if (!is_websocket_) {
    return Http::FilterDataStatus::Continue;
  }

  std::optional<std::vector<WebSocket::Frame>> frames = decoder_.decode(data);
  data.drain(data.length());

  if (frames.has_value()) {
    auto& limiter = getPerStreamRateLimiter();
    for (const auto& frame : frames.value()) {
      if (frame.opcode_ == WebSocket::kFrameOpcodeText ||
          frame.opcode_ == WebSocket::kFrameOpcodeBinary ||
          frame.opcode_ == WebSocket::kFrameOpcodeContinuation) {
        if (!limiter.requestAllowed({}).allowed) {
          config_->stats().rate_limited_.inc();
          if (!config_->rejectionMessage().empty()) {
            ENVOY_LOG(debug, "WebSocket message rate limit exceeded, notifying downstream client");
            sendRejectionFrame();
          } else {
            ENVOY_LOG(debug, "WebSocket message rate limit exceeded, dropping frame");
          }
          continue;
        }
        config_->stats().ok_.inc();
      }

      // encode the frame back to the outgoing data buffer, forwarding it upstream.
      std::optional<std::vector<uint8_t>> encoded_frame_header = encoder_.encodeFrameHeader(frame);
      if (encoded_frame_header.has_value()) {
        data.add(encoded_frame_header->data(), encoded_frame_header->size());
        if (frame.payload_ != nullptr) {
          data.add(*frame.payload_);
        }
      }
    }
  }

  return Http::FilterDataStatus::Continue;
}

Filters::Common::LocalRateLimit::LocalRateLimiterImpl&
WsLocalRateLimitFilter::getPerStreamRateLimiter() {
  auto typed_state =
      decoder_callbacks_->streamInfo().filterState()->getDataReadOnly<PerStreamRateLimiter>(
          PerStreamRateLimiter::key());

  if (typed_state == nullptr) {
    auto limiter = std::make_shared<PerStreamRateLimiter>(
        config_->fillInterval(), config_->maxTokens(), config_->tokensPerFill(),
        decoder_callbacks_->dispatcher());

    // Request-level (not Connection-level) life span: on HTTP/2 and HTTP/3, multiple
    // independent WebSocket sessions can be multiplexed over one downstream connection, and
    // each must get its own bucket rather than sharing one across the whole connection.
    decoder_callbacks_->streamInfo().filterState()->setData(
        PerStreamRateLimiter::key(), limiter, StreamInfo::FilterState::LifeSpan::Request);
    return limiter->value();
  }

  return const_cast<PerStreamRateLimiter&>(*typed_state).value();
}

void WsLocalRateLimitFilter::sendRejectionFrame() {
  // encodeData() below injects data into the encode/response filter chain. That's only valid
  // once response headers exist for this stream (e.g. the upstream's WebSocket upgrade response
  // has already been received and forwarded) - calling it before that violates FilterManager's
  // encode-iteration invariants (ASSERT(headers_continued_) in commonHandleAfterDataCallback)
  // and crashes. This can happen for real: a client bursting frames fast enough that the budget
  // is exceeded before the upstream's initial response headers arrive. Drop the notification in
  // that case rather than risk it - the offending frame itself is still dropped either way.
  if (!decoder_callbacks_->responseHeaders().has_value()) {
    ENVOY_LOG(debug, "Cannot notify downstream of WebSocket rate limit rejection: response "
                     "headers not sent yet");
    return;
  }

  // The rejection frame's bytes never change once the filter is configured, so they're computed
  // once in WsLocalRateLimitConfig rather than re-encoded on every single rejected frame.
  const std::optional<std::string>& encoded_frame = config_->encodedRejectionFrame();
  if (!encoded_frame.has_value()) {
    return;
  }
  Buffer::OwnedImpl reply_buffer(encoded_frame->data(), encoded_frame->size());

  // Send the frame directly to the downstream client on the encode path, bypassing upstream.
  decoder_callbacks_->encodeData(reply_buffer, false);
}

} // namespace WsLocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
