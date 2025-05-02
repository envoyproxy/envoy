#include "source/extensions/filters/network/common/redis/supported_commands.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

bool SupportedCommands::isSupportedCommand(const std::string& command) {
  return (simpleCommands().contains(command) || evalCommands().contains(command) ||
          hashMultipleSumResultCommands().contains(command) ||
          transactionCommands().contains(command) || auth() == command || echo() == command ||
          mget() == command || mset() == command || keys() == command || ping() == command ||
          time() == command || quit() == command || select() == command);
}

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
