#pragma once

#include <string>
#include <utility>

#include "absl/strings/string_view.h"

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
 */
class SseParser {
public:
  /**
   * Extracts and concatenates all 'data' field values from an SSE event.
   * Per SSE spec, multiple data fields are joined with newlines.
   *
   * @param event the complete SSE event string (from blank line to blank line).
   * @return concatenated data field values, or empty string if no data fields found.
   */
  static std::string extractDataField(absl::string_view event);

  /**
   * Finds the end of the next SSE event in the buffer.
   * An event ends with a blank line (two consecutive line breaks).
   *
   * @param str the buffer to search for an event.
   * @param end_stream whether this is the end of the stream (affects partial line handling).
   * @return a pair of {event_end, next_event_start} positions.
   *         Returns {npos, npos} if no complete event is found.
   */
  static std::pair<size_t, size_t> findEventEnd(absl::string_view str, bool end_stream);

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
