#include "source/common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Formatter {

const std::regex& SubstitutionFormatParser::commandWithArgsRegex() {
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
  // commands allow LENGTH to be specified, so not. Regex is used to validate the syntax and also
  // to extract values for COMMAND, SUBCOMMAND and LENGTH.
  //
  // Below is explanation of capturing and non-capturing groups. Non-capturing groups are used
  // to specify that certain part of the formatter command is optional and should contain specific
  // characters. Capturing groups are used to extract the values when regex is matched against
  // formatter command string.
  //
  // clang-format off
  // Non-capturing group specifying optional :LENGTH ----------------------
  //                                                                       |
  // Non-capturing group specifying optional (SUBCOMMAND)---               |
  //                                                        |              |
  // Non-capturing group specifying mandatory COMMAND       |              |
  //  which uses only A-Z, 0-9 and _ characters             |              |
  //  Group is used only to specify allowed characters.     |              |
  //                                      |                 |              |
  //                                      |                 |              |
  //                              _________________  _______________  _____________
  //                              |               |  |             |  |           |
  CONSTRUCT_ON_FIRST_USE(std::regex,
                         R"EOF(^%((?:[A-Z]|[0-9]|_)+)(?:\(([^\)]*)\))?(?::([0-9]+))?%)EOF");
  //                             |__________________|     |______|        |______|
  //                                      |                   |              |
  // Capturing group specifying COMMAND --                    |              |
  // The index of this group is 1.                            |              |
  //                                                          |              |
  // Capturing group for SUBCOMMAND. If present, it will -----               |
  // contain SUBCOMMAND without "(" and ")". The index                       |
  // of SUBCOMMAND group is 2.                                               |
  //                                                                         |
  // Capturing group for LENGTH. If present, it will -------------------------
  // contain just number without ":". The index of
  // LENGTH group is 3.
  // clang-format on
}

} // namespace Formatter
} // namespace Envoy
