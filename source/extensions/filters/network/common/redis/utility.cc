#include "extensions/filters/network/common/redis/utility.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Utility {

Redis::RespValue makeAuthCommand(const std::string& password) {
  std::vector<NetworkFilters::Common::Redis::RespValue> values(2);
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "auth";
  values[1].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[1].asString() = password;

  Redis::RespValue auth_command;
  auth_command.type(Redis::RespType::Array);
  auth_command.asArray().swap(values);
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

AskingRequest::AskingRequest() {
  std::vector<NetworkFilters::Common::Redis::RespValue> values(1);
  values[0].type(NetworkFilters::Common::Redis::RespType::BulkString);
  values[0].asString() = "asking";
  type(NetworkFilters::Common::Redis::RespType::Array);
  asArray().swap(values);
}

const AskingRequest& AskingRequest::instance() {
  static const AskingRequest* instance = new AskingRequest{};
  return *instance;
}

GetRequest::GetRequest() {
  type(RespType::BulkString);
  asString() = "get";
}

const GetRequest& GetRequest::instance() {
  static const GetRequest* instance = new GetRequest{};
  return *instance;
}

SetRequest::SetRequest() {
  type(RespType::BulkString);
  asString() = "set";
}

const SetRequest& SetRequest::instance() {
  static const SetRequest* instance = new SetRequest{};
  return *instance;
}
} // namespace Utility
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
