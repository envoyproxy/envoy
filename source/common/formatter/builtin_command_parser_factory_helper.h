#pragma once

#include <vector>

#include "envoy/formatter/substitution_formatter.h"

namespace Envoy {
namespace Formatter {

/**
 * Helper class to get all built-in command parsers for a given formatter context.
 */
class BuiltInCommandParserFactoryHelper {
public:
  using Factory = BuiltInCommandParserFactory;
  using Parsers = std::vector<CommandParserPtr>;

  /**
   * Get all built-in command parsers for a given formatter context.
   * @return Parsers all built-in command parsers for a given formatter context.
   */
  static const Parsers& commandParsers();
};

} // namespace Formatter
} // namespace Envoy
