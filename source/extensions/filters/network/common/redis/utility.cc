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

bool redirectionArgsInvalid(const Common::Redis::RespValue* original_request,
                            const Common::Redis::RespValue& error_response,
                            std::vector<absl::string_view>& error_substrings,
                            bool& ask_redirection) {
  if ((original_request == nullptr) || (error_response.type() != Common::Redis::RespType::Error)) {
    return true;
  }
  error_substrings = StringUtil::splitToken(error_response.asString(), " ", false);
  if (error_substrings.size() != 3) {
    return true;
  }
  if (error_substrings[0] == "ASK") {
    ask_redirection = true;
  } else if (error_substrings[0] == "MOVED") {
    ask_redirection = false;
  } else {
    // The first substring must be MOVED or ASK.
    return true;
  }
  // Other validation done later to avoid duplicate processing.
  return false;
}

RespValuePtr makeError(const std::string& error) {
  Common::Redis::RespValuePtr response(new RespValue());
  response->type(Common::Redis::RespType::Error);
  response->asString() = error;
  return response;
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
