#include "source/common/http/sse/sse_parser.h"

#include <algorithm>
#include <cstdint>

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {
namespace Sse {

SseParser::ParsedEvent SseParser::parseEvent(absl::string_view event) {
  // TODO(optimization): Consider merging findEventEnd and parseEvent into a single-pass
  // algorithm to avoid traversing the buffer twice.
  ParsedEvent parsed_event;
  absl::string_view remaining = event;

  while (!remaining.empty()) {
    auto [line_end, next_line] = findLineEnd(remaining, true);
    absl::string_view line = remaining.substr(0, line_end);
    remaining = remaining.substr(next_line);

    auto [field_name, field_value] = parseFieldLine(line);
    if (field_name == "data") {
      if (!parsed_event.data.has_value()) {
        // Optimization: Reserve memory to avoid allocations during append.
        // The total data cannot be larger than the input event string.
        parsed_event.data = std::string();
        parsed_event.data->reserve(event.size());
      } else {
        // Per SSE spec, multiple data fields are concatenated with newlines.
        parsed_event.data->append("\n");
      }
      parsed_event.data->append(field_value.data(), field_value.size());
    } else if (field_name == "id") {
      // Per SSE spec, if the field value contains U+0000 NULL, the field is ignored.
      // Otherwise, set the last event ID to the field value. If multiple id fields exist,
      // the last one wins.
      if (field_value.find('\0') == absl::string_view::npos) {
        parsed_event.id = std::string(field_value);
      }
    } else if (field_name == "event") {
      // Per SSE spec, the event field sets the event type. If multiple event fields exist,
      // the last one wins.
      parsed_event.event_type = std::string(field_value);
    } else if (field_name == "retry") {
      // Per SSE spec, the retry field must consist of only ASCII digits.
      // If it contains any other character, the field is ignored.
      if (!field_value.empty()) {
        uint64_t value = 0;
        bool valid = true;
        for (char c : field_value) {
          if (!absl::ascii_isdigit(c)) {
            valid = false;
            break;
          }
          uint64_t new_value = value * 10 + static_cast<uint64_t>(c - '0');
          if (new_value < value) {
            valid = false;
            break;
          }
          value = new_value;
        }
        if (valid) {
          parsed_event.retry = value;
        }
      }
    }
  }

  return parsed_event;
}

SseParser::FindEventEndResult SseParser::findEventEnd(absl::string_view buffer, bool end_stream) {
  size_t consumed = 0;
  size_t event_start = 0;
  absl::string_view remaining = buffer;

  // Per SSE spec: Strip UTF-8 BOM (0xEF 0xBB 0xBF) if present at stream start.
  if (consumed == 0 && remaining.size() >= 3 && static_cast<uint8_t>(remaining[0]) == 0xEF &&
      static_cast<uint8_t>(remaining[1]) == 0xBB && static_cast<uint8_t>(remaining[2]) == 0xBF) {
    remaining = remaining.substr(3);
    consumed = 3;
    event_start = 3; // Event content starts after BOM
  }

  while (!remaining.empty()) {
    auto [line_end, next_line] = findLineEnd(remaining, end_stream);

    if (line_end == absl::string_view::npos) {
      return {absl::string_view::npos, absl::string_view::npos, absl::string_view::npos};
    }

    if (line_end == 0) {
      // Found blank line so this is the end of event
      return {event_start, consumed, consumed + next_line};
    }

    consumed += next_line;
    remaining = remaining.substr(next_line);
  }

  // Per SSE spec: Once the end of the file is reached, any pending data must be discarded.
  // (i.e., incomplete events without a closing blank line are dropped)
  return {absl::string_view::npos, absl::string_view::npos, absl::string_view::npos};
}

std::pair<absl::string_view, absl::string_view> SseParser::parseFieldLine(absl::string_view line) {
  if (line.empty()) {
    return {"", ""};
  }

  // Per SSE spec, lines starting with ':' are comments and should be ignored.
  if (line[0] == ':') {
    return {"", ""};
  }

  const auto colon_pos = line.find(':');
  if (colon_pos == absl::string_view::npos) {
    return {line, ""};
  }

  absl::string_view field_name = line.substr(0, colon_pos);
  absl::string_view field_value = line.substr(colon_pos + 1);

  // Per SSE spec, remove leading space from value if present.
  if (!field_value.empty() && field_value[0] == ' ') {
    field_value = field_value.substr(1);
  }

  return {field_name, field_value};
}

std::pair<size_t, size_t> SseParser::findLineEnd(absl::string_view str, bool end_stream) {
  const auto pos = str.find_first_of("\r\n");

  // Case 1: No delimiter found
  if (pos == absl::string_view::npos) {
    if (end_stream) {
      return {str.size(), str.size()};
    }
    return {absl::string_view::npos, absl::string_view::npos};
  }

  // Case 2: LF (\n)
  if (str[pos] == '\n') {
    return {pos, pos + 1};
  }

  // Case 3: CR (\r) or CRLF (\r\n), handle per SSE spec
  if (pos + 1 < str.size()) {
    if (str[pos + 1] == '\n') {
      return {pos, pos + 2};
    }
    return {pos, pos + 1};
  }

  // Case 4: Split CRLF edge case
  // If '\r' is at the end and more data may come, wait to see if it's CRLF.
  if (end_stream) {
    return {pos, pos + 1};
  }
  return {absl::string_view::npos, absl::string_view::npos};
}

} // namespace Sse
} // namespace Http
} // namespace Envoy
