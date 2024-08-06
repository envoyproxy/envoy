#pragma once

#include <string>
#include <vector>

#include "envoy/http/protocol.h"
#include "envoy/stream_info/stream_info.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/types/optional.h"
#include "fmt/format.h"

namespace Envoy {
namespace Formatter {

class CommandSyntaxChecker {
public:
  using CommandSyntaxFlags = std::bitset<4>;
  static constexpr CommandSyntaxFlags COMMAND_ONLY = 0;
  static constexpr CommandSyntaxFlags PARAMS_REQUIRED = 1 << 0;
  static constexpr CommandSyntaxFlags PARAMS_OPTIONAL = 1 << 1;
  static constexpr CommandSyntaxFlags LENGTH_ALLOWED = 1 << 2;

  static absl::Status verifySyntax(CommandSyntaxChecker::CommandSyntaxFlags flags,
                                   absl::string_view command, absl::string_view subcommand,
                                   absl::optional<size_t> length);
};

/**
 * Context independent utility class for the formatter.
 */
class SubstitutionFormatUtils {
public:
  // Optional references are not supported, but this method has large performance
  // impact, so using reference_wrapper.
  static const absl::optional<std::reference_wrapper<const std::string>>
  protocolToString(const absl::optional<Http::Protocol>& protocol);
  static const std::string&
  protocolToStringOrDefault(const absl::optional<Http::Protocol>& protocol);
  static const absl::optional<std::string> getHostname();

  /**
   * Unspecified value for protobuf.
   */
  static const ProtobufWkt::Value& unspecifiedValue();

  /**
   * Truncate a string to a maximum length. Do nothing if max_length is not set or
   * max_length is greater than the length of the string.
   */
  static void truncate(std::string& str, absl::optional<size_t> max_length);

  /**
   * Truncate an input string view to a maximum length, and return the resulting string view. Do not
   * truncate if max_length is not set or max_length is greater than the length of the input string
   * view.
   */
  static absl::string_view truncateStringView(absl::string_view str,
                                              absl::optional<size_t> max_length);

  /**
   * Parse a header subcommand of the form: X?Y .
   * Will populate a main_header and an optional alternative header if specified.
   * See doc:
   * https://envoyproxy.io/docs/envoy/latest/configuration/observability/access_log/access_log#format-rules
   */
  using HeaderPair = std::pair<absl::string_view, absl::string_view>;
  static absl::StatusOr<HeaderPair> parseSubcommandHeaders(absl::string_view subcommand);

  /* Variadic function template to parse the
     subcommand and assign found tokens to sequence of params.
     subcommand must be a sequence
     of tokens separated by the same separator, like: header1:header2 or header1?header2?header3.
     params must be a sequence of std::string& with optional container storing std::string. Here are
     examples of params:
     - std::string& token1
     - std::string& token1, std::string& token2
     - std::string& token1, std::string& token2, std::vector<std::string>& remaining

     If command contains more tokens than number of passed params, unassigned tokens will be
     ignored. If command contains less tokens than number of passed params, some params will be left
     untouched.
  */
  template <typename... Tokens>
  static void parseSubcommand(absl::string_view subcommand, const char separator,
                              Tokens&&... params) {
    std::vector<absl::string_view> tokens = absl::StrSplit(subcommand, separator);
    std::vector<absl::string_view>::iterator it = tokens.begin();
    (
        [&](auto& param) {
          if (it != tokens.end()) {
            if constexpr (std::is_same_v<typename std::remove_reference<decltype(param)>::type,
                                         absl::string_view>) {
              // Compile time handler for absl::string_view.
              param = *it;
              it++;
            } else {
              // Compile time handler for container type. It will catch all remaining tokens and
              // move iterator to the end.
              do {
                param.push_back(*it);
                it++;
              } while (it != tokens.end());
            }
          }
        }(params),
        ...);
  }
};

} // namespace Formatter
} // namespace Envoy
