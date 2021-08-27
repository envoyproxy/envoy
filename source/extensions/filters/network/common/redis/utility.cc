#include "source/extensions/filters/network/common/redis/utility.h"

#include "source/common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Utility {

AuthRequest::AuthRequest(const std::string& password) {
  std::vector<RespValue> values(2);
  values[0].type(RespType::BulkString);
  values[0].asString() = "auth";
  values[1].type(RespType::BulkString);
  values[1].asString() = password;
  type(RespType::Array);
  asArray().swap(values);
}

AuthRequest::AuthRequest(const std::string& username, const std::string& password) {
  std::vector<RespValue> values(3);
  values[0].type(RespType::BulkString);
  values[0].asString() = "auth";
  values[1].type(RespType::BulkString);
  values[1].asString() = username;
  values[2].type(RespType::BulkString);
  values[2].asString() = password;
  type(RespType::Array);
  asArray().swap(values);
}

RespValuePtr makeError(const std::string& error) {
  Common::Redis::RespValuePtr response(new RespValue());
  response->type(Common::Redis::RespType::Error);
  response->asString() = error;
  return response;
}

ReadOnlyRequest::ReadOnlyRequest() {
  std::vector<RespValue> values(1);
  values[0].type(RespType::BulkString);
  values[0].asString() = "readonly";
  type(RespType::Array);
  asArray().swap(values);
}

const ReadOnlyRequest& ReadOnlyRequest::instance() {
  static const ReadOnlyRequest* instance = new ReadOnlyRequest{};
  return *instance;
}

AskingRequest::AskingRequest() {
  std::vector<RespValue> values(1);
  values[0].type(RespType::BulkString);
  values[0].asString() = "asking";
  type(RespType::Array);
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
