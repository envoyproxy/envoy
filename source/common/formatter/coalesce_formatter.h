#pragma once

#include <string>
#include <vector>

#include "envoy/formatter/substitution_formatter.h"
#include "envoy/json/json_object.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/statusor.h"
#include "source/common/formatter/substitution_format_utility.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Formatter {

/**
 * CoalesceFormatter provides a higher-order formatter that evaluates multiple
 * formatter operators in sequence and returns the first non-null result.
 *
 * This formatter accepts a JSON configuration specifying an array of operators
 * to evaluate. Each operator can be either:
 * - A string representing a simple command (e.g., "REQUESTED_SERVER_NAME")
 * - An object with "command" and optional "param" and "max_length" fields
 *
 * Example JSON configuration:
 * {
 *   "operators": [
 *     "REQUESTED_SERVER_NAME",
 *     {"command": "REQ", "param": ":authority"},
 *     {"command": "REQ", "param": "host"}
 *   ]
 * }
 *
 * Note that the JSON parameter cannot contain literal ')' characters as they would
 * interfere with the command parser regex.
 */
class CoalesceFormatter : public FormatterProvider {
public:
  /**
   * Creates a CoalesceFormatter from a JSON configuration string.
   * @param json_config the JSON configuration string.
   * @param max_length optional maximum length for the output.
   * @return StatusOr containing the formatter or an error.
   */
  static absl::StatusOr<FormatterProviderPtr> create(absl::string_view json_config,
                                                     absl::optional<size_t> max_length);

  CoalesceFormatter(std::vector<FormatterProviderPtr>&& formatters,
                    absl::optional<size_t> max_length)
      : formatters_(std::move(formatters)), max_length_(max_length) {}

  // FormatterProvider interface.
  absl::optional<std::string> format(const Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const override;
  Protobuf::Value formatValue(const Context& context,
                              const StreamInfo::StreamInfo& stream_info) const override;

private:
  /**
   * Parses a single operator entry from the JSON configuration.
   * @param entry the JSON object representing an operator entry.
   * @return StatusOr containing the formatter or an error.
   */
  static absl::StatusOr<FormatterProviderPtr> parseOperatorEntry(const Json::Object& entry);

  /**
   * Creates a formatter for the given command using built-in command parsers.
   * @param command the command name.
   * @param param the command parameter (may be empty).
   * @param max_length optional maximum length.
   * @return StatusOr containing the formatter or an error.
   */
  static absl::StatusOr<FormatterProviderPtr>
  createFormatterForCommand(absl::string_view command, absl::string_view param,
                            absl::optional<size_t> max_length);

  std::vector<FormatterProviderPtr> formatters_;
  absl::optional<size_t> max_length_;
};

} // namespace Formatter
} // namespace Envoy
