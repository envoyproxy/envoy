#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage_extractor.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

/**
 * All stats for the AI Protocol Manager filter. @see stats_macros.h
 */
#define ALL_AI_PROTOCOL_MANAGER_STATS(COUNTER)                                                     \
  COUNTER(token_usage_found)                                                                       \
  COUNTER(token_usage_partial)                                                                     \
  COUNTER(token_usage_missing)                                                                     \
  COUNTER(token_usage_total_mismatch)                                                              \
  COUNTER(token_usage_duplicate)                                                                   \
  COUNTER(response_parse_error)                                                                    \
  COUNTER(response_body_too_large)                                                                 \
  COUNTER(malformed_usage_field)                                                                   \
  COUNTER(sse_event_too_large)                                                                     \
  COUNTER(sse_incomplete_event)                                                                    \
  COUNTER(sse_event_budget_exhausted)                                                              \
  COUNTER(unsupported_content_encoding)

struct AiProtocolManagerStats {
  ALL_AI_PROTOCOL_MANAGER_STATS(GENERATE_COUNTER_STRUCT)
};

// Observe-only accumulator over a response body: the filter tees encode-path
// frames in, and no failure inside the handler can affect the stream. Results
// are read back after onEndStream().
//
// End-of-stream is an explicit signal rather than a flag on the last data
// frame: it must fire however the stream ends (end_stream data frame,
// possibly empty, or trailers) so pending parse work still resolves.
class ResponseHandler {
public:
  ResponseHandler(ApiFormat format, AiProtocolManagerStats& stats)
      : format_(format), stats_(stats) {}
  virtual ~ResponseHandler() = default;

  // Observe one response body frame (a copy; the original continues down the
  // filter chain untouched).
  virtual void onData(const Buffer::Instance& data) PURE;

  // The response body has ended (terminal data frame or trailers). Completes
  // any pending parsing and releases all buffered extraction state.
  virtual void onEndStream() PURE;

  const TokenUsage& usage() const { return usage_; }
  bool parsingComplete() const { return parsing_complete_; }
  // Extraction lost input this stream; accumulated usage may be stale or
  // incomplete and publishes as `extraction_status: partial`.
  bool degraded() const { return degraded_; }

protected:
  // Detect (if needed), extract, and merge one parsed JSON document -- an SSE
  // event's data payload, a whole JSON body, or one element of a streamed
  // array. Returns true when the document is the dialect's terminal event, so
  // callers stop processing later input against an authoritative result.
  bool processDocument(const Json::Object& json);

  ApiFormat format_;
  TokenUsage usage_;
  bool parsing_complete_{false};
  bool degraded_{false};
  AiProtocolManagerStats& stats_;
};

using ResponseHandlerPtr = std::unique_ptr<ResponseHandler>;

// Streaming SSE responses: OpenAI Chat Completions and Responses lifecycle
// events, Anthropic Messages events, Gemini `?alt=sse` chunks.
//
// Event framing is an incremental line-state scan: each byte is examined a
// bounded number of times regardless of fragmentation, and only complete,
// size-checked events are parsed. The same scanner state drives
// oversized-event discarding, so a dropped event's later `data:` lines can
// never be misread as a fresh event.
//
// Memory bound: retained state never exceeds max_event_size, regardless of
// individual frame size -- retention is gated before every copy, an over-cap
// complete event is never buffered, and discarding retains nothing. Retained
// bytes are charged to the stream's buffer memory account when present.
class SseResponseHandler : public ResponseHandler, public Logger::Loggable<Logger::Id::filter> {
public:
  SseResponseHandler(ApiFormat format, uint32_t max_event_size, uint32_t max_parsed_events,
                     AiProtocolManagerStats& stats,
                     const Buffer::BufferMemoryAccountSharedPtr& account = nullptr)
      : ResponseHandler(format, stats), max_event_size_(max_event_size),
        parse_budget_(max_parsed_events) {
    if (account != nullptr) {
      buffer_.bindAccount(account);
    }
  }

  void onData(const Buffer::Instance& data) override;
  void onEndStream() override;

private:
  friend class SseResponseHandlerPeer; // Test-only access to the retained buffer.

  // Line-state for the incremental event-boundary scanner. An SSE event ends
  // at a blank line; lines terminate with LF, CR, or CRLF.
  // TODO(botengyao): this bounded incremental decoder belongs in
  // source/common/http/sse next to SseParser, shared with sse_to_metadata, so
  // SSE framing semantics (CR/LF, BOM, EOF) have a single owner; coordinate
  // with HTTP maintainers before extracting it.
  enum class ScanState {
    LineStart,   // At the start of a line; nothing on it yet.
    LineContent, // The current line has at least one content byte.
    TermCr,      // A CR ended a content line; an immediate LF joins that terminator.
    BlankTermCr, // A CR ended a blank line; an immediate LF joins the boundary.
  };

  // Scans `data`, updating the line state. Returns the offset in `data` just
  // past an event-terminating blank line, or nullopt if none completes here.
  std::optional<uint64_t> scanView(absl::string_view data);
  // Processes one contiguous incoming region: scans for boundaries, retains
  // boundary-free bytes up to the cap (entering/leaving discard mode), and
  // consumes completed events. A complete event inside the region is handled
  // in place -- zero copy; only a split event goes through buffer_.
  void processSlice(absl::string_view view);
  // Handles the complete split event occupying buffer_; its linearization can
  // transiently hold a second copy (bounded by max_event_size).
  void consumeEvent();
  // A complete raw event region: classified by its `event:` field before any
  // materialization, parsed only if it survives classification and budget.
  void handleCompleteEvent(absl::string_view region);
  void enterDiscardMode();
  void processSseEvent(absl::string_view event);
  void retainBytes(absl::string_view bytes);

  const uint32_t max_event_size_;
  // Remaining parse budget (max_parsed_events); exhaustion makes the handler
  // inert with the accumulated usage flagged partial.
  uint32_t parse_budget_;
  bool budget_exhausted_{false};
  // Inside an over-cap unterminated event: nothing is retained, and the
  // scanner's line state finds the real terminating blank line.
  bool discarding_{false};
  ScanState scan_state_{ScanState::LineStart};
  // Invariant: only scanned, boundary-free bytes of the current pending
  // event, never exceeding max_event_size_.
  Buffer::OwnedImpl buffer_;
};

// Non-streaming JSON responses, including Gemini's default (non-SSE)
// streaming whose body is a root-level array of chunks. The body copy is
// capped at max_inspected_body_size (account-charged when present); larger
// bodies skip extraction.
class JsonResponseHandler : public ResponseHandler, public Logger::Loggable<Logger::Id::filter> {
public:
  JsonResponseHandler(ApiFormat format, uint32_t max_inspected_body_size,
                      AiProtocolManagerStats& stats,
                      const Buffer::BufferMemoryAccountSharedPtr& account = nullptr)
      : ResponseHandler(format, stats), max_inspected_body_size_(max_inspected_body_size) {
    if (account != nullptr) {
      buffer_.bindAccount(account);
    }
  }

  void onData(const Buffer::Instance& data) override;
  void onEndStream() override;

private:
  const uint32_t max_inspected_body_size_;
  bool over_limit_{false};
  Buffer::OwnedImpl buffer_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
