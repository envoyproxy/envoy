#pragma once

#include <string>

#include "common/common/utility.h"

#include "extensions/filters/network/common/redis/codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Utility {

Redis::RespValue makeAuthCommand(const std::string& password);

/**
 * Validate the received moved/ask redirection error and the original redis request.
 * @param[in] original_request supplies the incoming request associated with the command splitter
 * request.
 * @param[in] error_response supplies the moved/ask redirection response from the upstream Redis
 * server.
 * @param[out] error_substrings the non-whitespace substrings of error_response.
 * @param[out] ask_redirection true if error_response is an ASK redirection error, false otherwise.
 * @return bool true if the original_request or error_response are not valid, false otherwise.
 */
bool redirectionArgsInvalid(const Common::Redis::RespValue* original_request,
                            const Common::Redis::RespValue& error_response,
                            std::vector<absl::string_view>& error_substrings,
                            bool& ask_redirection);

RespValuePtr makeError(const std::string& error);

class ReadOnlyRequest : public Redis::RespValue {
public:
  ReadOnlyRequest();
  static const ReadOnlyRequest& instance();
};

class AskingRequest : public Redis::RespValue {
public:
  AskingRequest();
  static const AskingRequest& instance();
};

class GetRequest : public Redis::RespValue {
public:
  GetRequest();
  static const GetRequest& instance();
};

class SetRequest : public Redis::RespValue {
public:
  SetRequest();
  static const SetRequest& instance();
};

} // namespace Utility
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
