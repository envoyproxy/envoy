#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <utility>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Http {
namespace Sse {

/**
 * Parser for Server-Sent Events (SSE) format.
 * Implements the SSE specification: https://html.spec.whatwg.org/multipage/server-sent-events.html
 *
 * This parser handles:
 * - Multiple line ending formats (CR, LF, CRLF)
 * - Comment lines (lines starting with ':')
 * - Multiple data fields (concatenated with newlines)
 * - Partial events split across chunks
 * - End-of-stream handling
 *
 * Example usage:
 *   std::string buffer_;
 *   absl::string_view buffer_view(buffer_);
 *   while (!buffer_view.empty()) {
 *     auto [event_start, event_end, next_start] = findEventEnd(buffer_view, end_stream);
 *     if (event_start == absl::string_view::npos) break;
 *
 *     auto event_str = buffer_view.substr(event_start, event_end - event_start);
 *     auto event = parseEvent(event_str);
 *     if (event.data.has_value()) {
 *       // Process event.data.value()
 *     }
 *     buffer_view = buffer_view.substr(next_start);
 *   }
 *   buffer_.erase(0, buffer_.size() - buffer_view.size());
 */
class SseParser {
public:
  /**
   * Represents a parsed SSE event.
   * Supports 'data', 'id', 'event', and 'retry' fields per the SSE specification.
   */
  struct ParsedEvent {
    // The concatenated data field values. Per SSE spec, multiple data fields are joined with
    // newlines. absl::nullopt if no data fields present, empty string if data field exists but
    // empty.
    absl::optional<std::string> data;
    // The event ID. absl::nullopt if no id field is present.
    absl::optional<std::string> id;
    // The event type. absl::nullopt if no event field is present.
    absl::optional<std::string> event_type;
    // The reconnection time in milliseconds. absl::nullopt if no retry field is present.
    absl::optional<uint64_t> retry;
  };

  /**
   * Parses an SSE event and extracts fields.
   * Currently extracts only the 'data' field. Per SSE spec, multiple data fields are joined with
   * newlines.
   *
   * @param event the complete SSE event string (from blank line to blank line).
   * @return parsed event with available fields populated.
   */
  static ParsedEvent parseEvent(absl::string_view event);

  /**
   * Finds the end of the next SSE event in the buffer.
   * An event ends with a blank line (two consecutive line breaks).
   * Automatically handles UTF-8 BOM at the start of the stream.
   *
   * @param buffer the buffer to search for an event.
   * @param end_stream whether this is the end of the stream (affects partial line handling).
   * @return a tuple of {event_start, event_end, next_event_start} positions.
   *         event_start: where the event content begins (after BOM if present)
   *         event_end: where the event content ends (excluding trailing blank line)
   *         next_event_start: where to continue parsing for the next event
   *         Returns {npos, npos, npos} if no complete event is found.
   */
  static std::tuple<size_t, size_t, size_t> findEventEnd(absl::string_view buffer, bool end_stream);

private:
  /**
   * Parses an SSE field line into {field_name, field_value}.
   * Handles comments (lines starting with ':') and strips leading space from value.
   *
   * @param line a single line from an SSE event.
   * @return a pair of {field_name, field_value}. Returns {"", ""} for empty lines or comments.
   */
  static std::pair<absl::string_view, absl::string_view> parseFieldLine(absl::string_view line);

  /**
   * Finds the end of the current line, handling CR, LF, and CRLF line endings.
   * Per SSE spec, all three line ending formats are supported.
   *
   * @param str the string to search for a line ending.
   * @param end_stream whether this is the end of the stream (affects partial line handling).
   * @return a pair of {line_end, next_line_start} positions.
   *         Returns {npos, npos} if no complete line is found.
   */
  static std::pair<size_t, size_t> findLineEnd(absl::string_view str, bool end_stream);
};

} // namespace Sse
} // namespace Http
} // namespace Envoy
