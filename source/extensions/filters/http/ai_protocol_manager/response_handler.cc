#include "source/extensions/filters/http/ai_protocol_manager/response_handler.h"

#include <string>

#include "source/common/common/assert.h"
#include "source/common/http/sse/sse_parser.h"
#include "source/common/json/json_loader.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {
// OpenAI Chat Completions stream terminator; not JSON, so it is recognized
// before parsing.
constexpr absl::string_view OpenAiDoneSentinel{"[DONE]"};

// Returns the last `event:` field value in a raw event region as a view (per
// the SSE spec the last occurrence wins; one leading space is stripped).
std::optional<absl::string_view> sseEventTypeView(absl::string_view region) {
  std::optional<absl::string_view> event_type;
  size_t pos = 0;
  while (pos < region.size()) {
    size_t eol = region.find_first_of("\r\n", pos);
    if (eol == absl::string_view::npos) {
      eol = region.size();
    }
    absl::string_view line = region.substr(pos, eol - pos);
    if (absl::ConsumePrefix(&line, "event:")) {
      absl::ConsumePrefix(&line, " ");
      event_type = line;
    }
    pos = eol;
    if (pos < region.size() && region[pos] == '\r') {
      ++pos;
    }
    if (pos < region.size() && region[pos] == '\n') {
      ++pos;
    }
  }
  return event_type;
}

// Named events that cannot carry usage or terminate extraction, dropped
// before the event's data is assembled or parsed. Unnamed events (OpenAI Chat
// Completions) cannot be classified and are bounded by the parse budget.
bool skippableEventType(absl::string_view event_type) {
  // Anthropic keepalives and content deltas.
  if (event_type == "ping" || absl::StartsWith(event_type, "content_block")) {
    return true;
  }
  // OpenAI Responses lifecycle events: usage rides only the terminal ones.
  if (absl::StartsWith(event_type, "response.")) {
    return !(event_type == "response.completed" || event_type == "response.failed" ||
             event_type == "response.incomplete");
  }
  return false;
}
} // namespace

bool ResponseHandler::processDocument(const Json::Object& json) {
  if (!json.isObject()) {
    // Non-object JSON (scalar, array, null) is an unsupported shape, not an
    // error -- and it must not reach the object-only accessors below, which
    // treat invocation on a non-object node as an internal bug.
    return false;
  }
  if (format_ == ApiFormat::Unknown) {
    format_ = TokenUsageExtractor::detectFormat(json);
    if (format_ == ApiFormat::Unknown) {
      return false; // Not discriminating; a later document may be.
    }
  }
  const ExtractionResult result = TokenUsageExtractor::extract(format_, json);
  usage_.merge(result.usage);
  if (result.malformed) {
    // A corrupt document must not leave an earlier accumulated snapshot
    // published as complete.
    stats_.malformed_usage_field_.inc();
    degraded_ = true;
  }
  return TokenUsageExtractor::isTerminalEvent(format_, json);
}

std::optional<uint64_t> SseResponseHandler::scanView(absl::string_view data) {
  for (size_t i = 0; i < data.size(); ++i) {
    const char c = data[i];
    switch (scan_state_) {
    case ScanState::LineStart:
      if (c == '\n') {
        return i + 1; // LF-terminated blank line: event boundary.
      }
      if (c == '\r') {
        scan_state_ = ScanState::BlankTermCr;
      } else {
        scan_state_ = ScanState::LineContent;
      }
      break;
    case ScanState::LineContent:
      if (c == '\n') {
        scan_state_ = ScanState::LineStart;
      } else if (c == '\r') {
        scan_state_ = ScanState::TermCr;
      }
      break;
    case ScanState::TermCr:
      // The CR already ended the content line.
      if (c == '\n') {
        scan_state_ = ScanState::LineStart; // CRLF pair.
      } else if (c == '\r') {
        scan_state_ = ScanState::BlankTermCr; // A new, empty line ended by CR.
      } else {
        scan_state_ = ScanState::LineContent; // A new line starting with content.
      }
      break;
    case ScanState::BlankTermCr:
      // A blank line ended with CR: the boundary is complete, but an
      // immediately following LF belongs to the same CRLF terminator.
      scan_state_ = ScanState::LineStart;
      if (c == '\n') {
        return i + 1;
      }
      return i; // Boundary before this byte; it is re-scanned as next-event input.
    }
  }
  return std::nullopt;
}

void SseResponseHandler::onData(const Buffer::Instance& data) {
  if (parsing_complete_ || budget_exhausted_ || data.length() == 0) {
    return;
  }
  // Incoming frames are scanned directly, before any retention; previously
  // buffered slices are never re-enumerated.
  for (const Buffer::RawSlice& slice : data.getRawSlices()) {
    if (parsing_complete_) {
      break;
    }
    processSlice(absl::string_view(static_cast<const char*>(slice.mem_), slice.len_));
  }
  if (parsing_complete_) {
    // Terminal event seen: release everything; later frames drop at entry.
    buffer_.drain(buffer_.length());
  }
}

void SseResponseHandler::retainBytes(absl::string_view bytes) { buffer_.add(bytes); }

void SseResponseHandler::processSlice(absl::string_view view) {
  while (!view.empty() && !parsing_complete_ && !budget_exhausted_) {
    const auto boundary = scanView(view);
    if (discarding_) {
      // Nothing is retained while discarding; the scanner's line state (not
      // raw tail bytes) carries the mid-event position across frames, so a
      // split terminator cannot fabricate a boundary.
      if (!boundary.has_value()) {
        return; // The rest of the slice belongs to the discarded event.
      }
      discarding_ = false;
      view.remove_prefix(boundary.value());
      continue;
    }
    // buffer_.length() <= max_event_size_ is a class invariant, so the
    // subtraction cannot underflow.
    const uint64_t allowance = static_cast<uint64_t>(max_event_size_) - buffer_.length();
    if (!boundary.has_value()) {
      // The whole (already scanned) view is interior of the pending event.
      // The cap is enforced *before* copying: one oversized frame must not
      // become an over-cap side allocation.
      if (view.size() > allowance) {
        enterDiscardMode();
      } else {
        retainBytes(view);
      }
      return;
    }
    if (boundary.value() > allowance) {
      // A complete over-cap event is skipped without ever being buffered,
      // linearized, or parsed.
      ENVOY_LOG(debug, "ai_protocol_manager: SSE event exceeds max_event_size ({} > {}), skipped",
                buffer_.length() + boundary.value(), max_event_size_);
      stats_.sse_event_too_large_.inc();
      degraded_ = true;
      buffer_.drain(buffer_.length());
      // scanView() already left the line state at LineStart.
    } else if (buffer_.length() == 0) {
      // Fast path: process the complete event in place -- nothing retained.
      handleCompleteEvent(view.substr(0, boundary.value()));
    } else {
      retainBytes(view.substr(0, boundary.value()));
      consumeEvent();
    }
    view.remove_prefix(boundary.value());
  }
}

void SseResponseHandler::consumeEvent() {
  const uint64_t length = buffer_.length();
  ASSERT(length <= max_event_size_);
  // Only split events reach here; linearize() can transiently hold a second,
  // account-charged copy (bounded by max_event_size) while consolidating.
  const absl::string_view region(static_cast<const char*>(buffer_.linearize(length)), length);
  handleCompleteEvent(region);
  buffer_.drain(buffer_.length());
  scan_state_ = ScanState::LineStart;
}

void SseResponseHandler::handleCompleteEvent(absl::string_view region) {
  // Classify before materializing: named non-usage events are dropped before
  // their data payload is assembled or parsed.
  if (const auto event_type = sseEventTypeView(region);
      event_type.has_value() && skippableEventType(event_type.value())) {
    return;
  }
  // The region ends exactly at a blank-line boundary, so end_stream=true
  // resolves a trailing bare-CR terminator safely.
  const auto result = Http::Sse::SseParser::findEventEnd(region, /*end_stream=*/true);
  if (result.event_start != absl::string_view::npos) {
    processSseEvent(region.substr(result.event_start, result.event_end - result.event_start));
  }
}

void SseResponseHandler::enterDiscardMode() {
  // The pending event exceeded the cap before terminating: drop its bytes,
  // keep only the scanner's line state. Counted once per oversized event.
  ENVOY_LOG(debug,
            "ai_protocol_manager: pending SSE data exceeds max_event_size ({}), "
            "discarding this event",
            max_event_size_);
  stats_.sse_event_too_large_.inc();
  degraded_ = true;
  discarding_ = true;
  buffer_.drain(buffer_.length());
}

void SseResponseHandler::onEndStream() {
  if (!parsing_complete_ && !budget_exhausted_ && !discarding_ && buffer_.length() > 0) {
    // Final pass with EOS semantics: a pending event whose terminator only
    // resolves at EOS (e.g. a trailing bare CR) completes here; genuinely
    // incomplete events are discarded per the SSE spec.
    const uint64_t length = buffer_.length();
    absl::string_view view(static_cast<const char*>(buffer_.linearize(length)), length);
    while (!view.empty() && !parsing_complete_ && !budget_exhausted_) {
      const auto result = Http::Sse::SseParser::findEventEnd(view, /*end_stream=*/true);
      if (result.event_start == absl::string_view::npos) {
        break;
      }
      const absl::string_view event =
          view.substr(result.event_start, result.event_end - result.event_start);
      if (const auto event_type = sseEventTypeView(event);
          !event_type.has_value() || !skippableEventType(event_type.value())) {
        processSseEvent(event);
      }
      view = view.substr(result.next_start);
    }
    if (!parsing_complete_ && !budget_exhausted_ && !view.empty()) {
      // Discarded incomplete input may have been the usage-bearing final
      // event; an earlier snapshot must not publish as complete.
      ENVOY_LOG(debug,
                "ai_protocol_manager: {} bytes of incomplete SSE input discarded at end of stream",
                view.size());
      stats_.sse_incomplete_event_.inc();
      degraded_ = true;
    }
  }
  // All extraction state is released at EOS regardless of outcome.
  buffer_.drain(buffer_.length());
}

void SseResponseHandler::processSseEvent(absl::string_view event) {
  const auto parsed_event = Http::Sse::SseParser::parseEvent(event);
  if (!parsed_event.data.has_value() || parsed_event.data.value().empty()) {
    // Comment-only keepalives and events without data are normal; skip.
    return;
  }
  const std::string& data = parsed_event.data.value();
  if (data == OpenAiDoneSentinel) {
    parsing_complete_ = true;
    return;
  }

  // Stream-wide parse budget: bounds how many payloads a long-lived stream
  // can make the worker JSON-parse. Exhaustion makes the handler inert.
  if (parse_budget_ == 0) {
    ENVOY_LOG(debug, "ai_protocol_manager: SSE parse budget exhausted, extraction goes inert");
    stats_.sse_event_budget_exhausted_.inc();
    budget_exhausted_ = true;
    degraded_ = true;
    return;
  }
  --parse_budget_;

  auto json_or = Json::Factory::loadFromString(data);
  if (!json_or.ok() || json_or.value() == nullptr) {
    // Never log payload bytes: they can carry model output or user data.
    ENVOY_LOG(debug, "ai_protocol_manager: {}-byte SSE event data is not valid JSON, skipped",
              data.size());
    stats_.response_parse_error_.inc();
    degraded_ = true;
    return;
  }
  if (processDocument(*json_or.value())) {
    parsing_complete_ = true;
  }
}

void JsonResponseHandler::onData(const Buffer::Instance& data) {
  if (over_limit_ || parsing_complete_) {
    return;
  }
  // buffer_.length() <= max_inspected_body_size_ is an invariant, so the
  // subtraction cannot underflow.
  if (data.length() > max_inspected_body_size_ - buffer_.length()) {
    ENVOY_LOG(debug,
              "ai_protocol_manager: response body exceeds max_inspected_body_size ({}), "
              "skipping token extraction",
              max_inspected_body_size_);
    stats_.response_body_too_large_.inc();
    over_limit_ = true;
    degraded_ = true;
    buffer_.drain(buffer_.length());
    return;
  }
  buffer_.add(data);
}

void JsonResponseHandler::onEndStream() {
  if (over_limit_ || parsing_complete_) {
    return;
  }
  parsing_complete_ = true;
  auto json_or = Json::Factory::loadFromString(buffer_.toString());
  buffer_.drain(buffer_.length());
  if (!json_or.ok() || json_or.value() == nullptr) {
    // Never log payload bytes; see the SSE parse-error note.
    ENVOY_LOG(debug, "ai_protocol_manager: response body is not valid JSON");
    stats_.response_parse_error_.inc();
    degraded_ = true;
    return;
  }
  const Json::ObjectSharedPtr& root = json_or.value();

  // A root-level array is Gemini's default (non-SSE) streaming. Elements are
  // processed in order, stopping at a terminal event so a later element
  // cannot override an authoritative terminal result.
  if (root->isArray()) {
    auto elements_or = root->asObjectArray();
    if (!elements_or.ok()) {
      stats_.response_parse_error_.inc();
      degraded_ = true;
      return;
    }
    for (const Json::ObjectSharedPtr& element : elements_or.value()) {
      if (element != nullptr && processDocument(*element)) {
        break;
      }
    }
    return;
  }

  processDocument(*root);
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
