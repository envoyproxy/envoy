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
 *     auto result = findEventEnd(buffer_view, end_stream);
 *     if (result.event_start == absl::string_view::npos) break;
 *
 *     auto event_str = buffer_view.substr(result.event_start, result.event_end -
 * result.event_start); auto event = parseEvent(event_str); if (event.data.has_value()) {
 *       // Process event.data.value()
 *     }
 *     buffer_view = buffer_view.substr(result.next_start);
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
   * Result of finding the end of an SSE event in a buffer.
   */
  struct FindEventEndResult {
    // Where the event content begins (after BOM if present).
    size_t event_start;
    // Where the event content ends (excluding trailing blank line).
    size_t event_end;
    // Where to continue parsing for the next event.
    size_t next_start;
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
   * @return FindEventEndResult with event_start, event_end, and next_start positions.
   *         All fields are set to npos if no complete event is found.
   */
  static FindEventEndResult findEventEnd(absl::string_view buffer, bool end_stream);

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
