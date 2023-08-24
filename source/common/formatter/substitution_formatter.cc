#include "source/common/formatter/substitution_formatter.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/upstream/upstream.h"

#include "source/common//formatter/http_specific_formatter.h"
#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/thread.h"
#include "source/common/common/utility.h"
#include "source/common/formatter/http_specific_formatter.h"
#include "source/common/formatter/stream_info_formatter.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "fmt/format.h"

namespace Envoy {
namespace Formatter {

std::vector<FormatterProviderPtr> SubstitutionFormatParser::parse(const std::string& format) {
  return SubstitutionFormatParser::parse(format, {});
}

// TODO(derekargueta): #2967 - Rewrite SubstitutionFormatter with parser library & formal grammar
std::vector<FormatterProviderPtr>
SubstitutionFormatParser::parse(const std::string& format,
                                const std::vector<CommandParserPtr>& commands) {
  std::string current_token;
  std::vector<FormatterProviderPtr> formatters;

  // The following regex is used to check validity of the formatter command and to
  // extract groups.
  // The formatter command has the following format:
  //    % COMMAND(SUBCOMMAND):LENGTH%
  // % signs at the beginning and end are used by parser to find next COMMAND.
  // COMMAND must always be present and must consist of characters: "A-Z", "0-9" or "_".
  // SUBCOMMAND presence depends on the COMMAND. Format is flexible but cannot contain ")".:
  // - for some commands SUBCOMMAND is not allowed (for example %PROTOCOL%)
  // - for some commands SUBCOMMAND is required (for example %REQ(:AUTHORITY)%, just %REQ% will
  // cause error)
  // - for some commands SUBCOMMAND is optional (for example %START_TIME% and
  // %START_TIME(%f.%1f.%2f.%3f)% are both correct).
  // LENGTH presence depends on the command. Some
  // commands allow LENGTH to be specified, so not. Regex is used to validate the syntax and also to
  // extract values for COMMAND, SUBCOMMAND and LENGTH.
  //
  // Below is explanation of capturing and non-capturing groups. Non-capturing groups are used
  // to specify that certain part of the formatter command is optional and should contain specific
  // characters. Capturing groups are used to extract the values when regex is matched against
  // formatter command string.
  //
  // clang-format off
  // Non-capturing group specifying optional :LENGTH -------------------------------------
  //                                                                                      |
  // Non-capturing group specifying optional (SUBCOMMAND)------------------               |
  //                                                                       |              |
  // Non-capturing group specifying mandatory COMMAND                      |              |
  //  which uses only A-Z, 0-9 and _ characters     -----                  |              |
  //  Group is used only to specify allowed characters.  |                 |              |
  //                                                     |                 |              |
  //                                                     |                 |              |
  //                                             _________________  _______________  _____________
  //                                             |               |  |             |  |           |
  const std::regex command_w_args_regex(R"EOF(^%((?:[A-Z]|[0-9]|_)+)(?:\(([^\)]*)\))?(?::([0-9]+))?%)EOF");
  //                                            |__________________|     |______|        |______|
  //                                                     |                   |              |
  // Capturing group specifying COMMAND -----------------                    |              |
  // The index of this group is 1.                                           |              |
  //                                                                         |              |
  // Capturing group for SUBCOMMAND. If present, it will --------------------               |
  // contain SUBCOMMAND without "(" and ")". The index                                      |
  // of SUBCOMMAND group is 2.                                                              |
  //                                                                                        |
  // Capturing group for LENGTH. If present, it will ----------------------------------------
  // contain just number without ":". The index of
  // LENGTH group is 3.
  // clang-format on

  for (size_t pos = 0; pos < format.size(); ++pos) {
    if (format[pos] != '%') {
      current_token += format[pos];
      continue;
    }

    // escape '%%'
    if (format.size() > pos + 1) {
      if (format[pos + 1] == '%') {
        current_token += '%';
        pos++;
        continue;
      }
    }

    if (!current_token.empty()) {
      formatters.emplace_back(FormatterProviderPtr{new PlainStringFormatter(current_token)});
      current_token = "";
    }

    std::smatch m;
    const std::string search_space = format.substr(pos);
    if (!std::regex_search(search_space, m, command_w_args_regex)) {
      throw EnvoyException(fmt::format(
          "Incorrect configuration: {}. Couldn't find valid command at position {}", format, pos));
    }

    const std::string match = m.str(0);
    // command is at at index 1.
    const std::string command = m.str(1);
    // subcommand is at index 2.
    const std::string subcommand = m.str(2);
    // optional length is at index 3. If present, validate that it is valid integer.
    absl::optional<size_t> max_length;
    if (m.str(3).length() != 0) {
      size_t length_value;
      if (!absl::SimpleAtoi(m.str(3), &length_value)) {
        throw EnvoyException(absl::StrCat("Length must be an integer, given: ", m.str(3)));
      }
      max_length = length_value;
    }
    std::vector<std::string> path;

    const size_t command_end_position = pos + m.str(0).length() - 1;

    auto formatter =
        HttpBuiltInCommandParser::builtInCommandParser().parse(command, subcommand, max_length);
    if (formatter) {
      formatters.push_back(std::move(formatter));
    } else {
      // Check formatter extensions. These are used for anything not provided by the built-in
      // operators, e.g.: specialized formatting, computing stats from request/response headers
      // or from stream info, etc.
      bool added = false;
      for (const auto& cmd : commands) {
        auto formatter = cmd->parse(command, subcommand, max_length);
        if (formatter) {
          formatters.push_back(std::move(formatter));
          added = true;
          break;
        }
      }

      if (!added) {
        formatters.emplace_back(
            FormatterProviderPtr{new StreamInfoFormatter(command, subcommand, max_length)});
      }
    }

    pos = command_end_position;
  }

  if (!current_token.empty() || format.empty()) {
    // Create a PlainStringFormatter with the final string literal. If the format string was empty,
    // this creates a PlainStringFormatter with an empty string.
    formatters.emplace_back(FormatterProviderPtr{new PlainStringFormatter(current_token)});
  }

  return formatters;
}

} // namespace Formatter
} // namespace Envoy
