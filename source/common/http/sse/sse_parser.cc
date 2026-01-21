#include "source/common/http/sse/sse_parser.h"

#include <vector>

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {
namespace Sse {

SseParser::ParsedEvent SseParser::parseEvent(absl::string_view event) {
  std::vector<absl::string_view> data_fields;
  absl::string_view remaining = event;

  while (!remaining.empty()) {
    auto [line_end, next_line] = findLineEnd(remaining, true);
    absl::string_view line = remaining.substr(0, line_end);
    remaining = remaining.substr(next_line);

    auto [field_name, field_value] = parseFieldLine(line);
    if (field_name == "data") {
      data_fields.push_back(field_value);
    }
  }

  ParsedEvent parsed_event;
  // Per SSE spec, multiple data fields are concatenated with newlines.
  if (!data_fields.empty()) {
    parsed_event.data = absl::StrJoin(data_fields, "\n");
  }
  return parsed_event;
}

std::pair<size_t, size_t> SseParser::findEventEnd(absl::string_view buffer, bool end_stream) {
  size_t consumed = 0;
  absl::string_view remaining = buffer;

  while (!remaining.empty()) {
    auto [line_end, next_line] = findLineEnd(remaining, end_stream);

    if (line_end == absl::string_view::npos) {
      return {absl::string_view::npos, absl::string_view::npos};
    }

    if (line_end == 0) {
      return {consumed, consumed + next_line};
    }

    consumed += next_line;
    remaining = remaining.substr(next_line);
  }

  return {absl::string_view::npos, absl::string_view::npos};
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

  if (pos == absl::string_view::npos) {
    if (end_stream) {
      return {str.size(), str.size()};
    }
    return {absl::string_view::npos, absl::string_view::npos};
  }

  if (str[pos] == '\n') {
    return {pos, pos + 1};
  }

  // Per SSE spec, handle CR (\r) and CRLF (\r\n) line endings.
  if (pos + 1 < str.size()) {
    if (str[pos + 1] == '\n') {
      return {pos, pos + 2};
    }
    return {pos, pos + 1};
  }

  // If '\r' is at the end and more data may come, wait to see if it's CRLF.
  if (end_stream) {
    return {pos, pos + 1};
  }
  return {absl::string_view::npos, absl::string_view::npos};
}

} // namespace Sse
} // namespace Http
} // namespace Envoy
