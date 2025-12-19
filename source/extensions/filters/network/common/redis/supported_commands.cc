#include "source/extensions/filters/network/common/redis/supported_commands.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

bool SupportedCommands::isSupportedCommand(const std::string& command) {
  return (simpleCommands().contains(command) || evalCommands().contains(command) ||
          hashMultipleSumResultCommands().contains(command) ||
          ClusterScopeCommands().contains(command) || randomShardCommands().contains(command) ||
          transactionCommands().contains(command) || auth() == command || echo() == command ||
          mget() == command || mset() == command || ping() == command || time() == command ||
          quit() == command || scan() == command || infoShard() == command);
}

bool SupportedCommands::isCommandValidWithoutArgs(const std::string& command_name) {
  // Transaction commands are valid without mandatory arguments
  if (transactionCommands().contains(command_name)) {
    return true;
  }

  // Commands that are explicitly valid without mandatory arguments
  return commandsWithoutMandatoryArgs().contains(command_name);
}

bool SupportedCommands::validateCommandSubcommands(const std::string& command,
                                                   const std::string& subcommand) {
  const CommandSubcommandMap& validation_map = commandSubcommandValidationMap();

  // If command is not in validation map, all subcommands are allowed
  auto it = validation_map.find(command);
  if (it == validation_map.end()) {
    return true; // No validation needed - all forms of the command are supported
  }

  // Command is in validation map, so it has subcommand restrictions
  // Validate the subcommand against the allowlist
  const auto& allowed_subcommands = it->second;

  return allowed_subcommands.find(subcommand) != allowed_subcommands.end();
}

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
