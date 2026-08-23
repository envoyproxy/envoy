#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/websocket/codec.h"
#include "source/extensions/filters/common/local_ratelimit/local_ratelimit_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "contrib/envoy/extensions/filters/http/ws_local_ratelimit/v3alpha/ws_local_ratelimit.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace WsLocalRateLimitFilter {

/**
 * All WebSocket local rate limit stats. @see stats_macros.h
 */
#define ALL_WS_LOCAL_RATE_LIMIT_STATS(COUNTER)                                                     \
  COUNTER(ok)                                                                                      \
  COUNTER(rate_limited)

struct WsLocalRateLimitStats {
  ALL_WS_LOCAL_RATE_LIMIT_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Global configuration for the WebSocket local rate limit filter. Holds the per-connection
 * token bucket parameters; there is no shared/global bucket for this filter.
 */
class WsLocalRateLimitConfig {
public:
  WsLocalRateLimitConfig(
      const envoy::extensions::filters::http::ws_local_ratelimit::v3alpha::WsLocalRateLimit& config,
      Stats::Scope& scope);

  const std::chrono::milliseconds& fillInterval() const { return fill_interval_; }
  uint32_t maxTokens() const { return max_tokens_; }
  uint32_t tokensPerFill() const { return tokens_per_fill_; }
  // Empty means no response is sent to the client; the rejected frame is just dropped.
  const std::string& rejectionMessage() const { return rejection_message_; }
  // rejectionMessage() pre-encoded as a complete WebSocket text frame (header + payload), ready
  // to copy onto the wire as-is. Computed once here since rejectionMessage() never changes after
  // construction. Unset when rejectionMessage() is empty (nothing to send).
  const std::optional<std::string>& encodedRejectionFrame() const {
    return encoded_rejection_frame_;
  }
  WsLocalRateLimitStats& stats() const { return stats_; }

private:
  static WsLocalRateLimitStats generateStats(const std::string& prefix, Stats::Scope& scope);
  static std::optional<std::string> encodeRejectionFrame(const std::string& rejection_message);

  const std::chrono::milliseconds fill_interval_;
  const uint32_t max_tokens_;
  const uint32_t tokens_per_fill_;
  const std::string rejection_message_;
  const std::optional<std::string> encoded_rejection_frame_;
  mutable WsLocalRateLimitStats stats_;
};

using WsLocalRateLimitConfigSharedPtr = std::shared_ptr<WsLocalRateLimitConfig>;

/**
 * Wraps a LocalRateLimiterImpl configured with no descriptors, so it only ever exposes the
 * single default token bucket. One instance is stored per downstream HTTP stream (i.e. per
 * WebSocket session) in StreamInfo::FilterState (LifeSpan::Request), giving every WebSocket
 * session its own, independent bucket with no descriptor matching involved. Request-level
 * scoping (rather than Connection-level) matters because HTTP/2 and HTTP/3 can multiplex
 * multiple independent WebSocket sessions (RFC 8441 / RFC 9220 extended CONNECT) over a single
 * downstream connection; Connection-level storage would incorrectly make them share one bucket.
 */
class PerStreamRateLimiter : public StreamInfo::FilterState::Object {
public:
  PerStreamRateLimiter(const std::chrono::milliseconds& fill_interval, uint32_t max_tokens,
                       uint32_t tokens_per_fill, Event::Dispatcher& dispatcher)
      : rate_limiter_(fill_interval, max_tokens, tokens_per_fill, dispatcher,
                      /*descriptors=*/{}, /*always_consume_default_token_bucket=*/true) {}

  static const std::string& key();

  Filters::Common::LocalRateLimit::LocalRateLimiterImpl& value() { return rate_limiter_; }

private:
  Filters::Common::LocalRateLimit::LocalRateLimiterImpl rate_limiter_;
};

/**
 * HTTP filter that rate limits WebSocket data frames (text, binary, continuation) on a
 * per-WebSocket-session basis. Non-WebSocket requests are passed through untouched.
 */
class WsLocalRateLimitFilter : public Http::PassThroughFilter,
                               public Logger::Loggable<Logger::Id::filter> {
public:
  explicit WsLocalRateLimitFilter(WsLocalRateLimitConfigSharedPtr config)
      : config_(std::move(config)), decoder_(WebSocket::kMaxPayloadBufferLength) {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

private:
  Filters::Common::LocalRateLimit::LocalRateLimiterImpl& getPerStreamRateLimiter();
  void sendRejectionFrame();

  WsLocalRateLimitConfigSharedPtr config_;
  bool is_websocket_{false};
  WebSocket::Decoder decoder_;
  WebSocket::Encoder encoder_;
};

} // namespace WsLocalRateLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
