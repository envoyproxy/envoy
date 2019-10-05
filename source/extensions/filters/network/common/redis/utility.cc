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

ReadOnlyRequest::ReadOnlyRequest() {
  std::vector<NetworkFilters::Common::Redis::RespValue> values(1);
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "readonly";
  type(NetworkFilters::Common::Redis::RespType::Array);
  asArray().swap(values);
}

const ReadOnlyRequest& ReadOnlyRequest::instance() {
  static const ReadOnlyRequest* instance = new ReadOnlyRequest{};
  return *instance;
}

} // namespace Utility
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
