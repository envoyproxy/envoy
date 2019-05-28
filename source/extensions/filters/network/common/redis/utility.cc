#include "extensions/filters/network/common/redis/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Utility {

Redis::RespValue makeAuthCommand(const std::string& password) {
  Redis::RespValue auth_command, value;
  auth_command.type(Redis::RespType::Array);
  value.type(Redis::RespType::BulkString);
  value.asString() = "auth";
  auth_command.asArray().push_back(value);
  value.asString() = password;
  auth_command.asArray().push_back(value);
  return auth_command;
}

} // namespace Utility
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
