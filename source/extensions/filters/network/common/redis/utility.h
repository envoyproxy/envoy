#pragma once

#include <string>
#include <vector>

#include "source/extensions/filters/network/common/redis/codec.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {
namespace Utility {

class AuthRequest : public Redis::RespValue {
public:
  AuthRequest(const std::string& username, const std::string& password);
  AuthRequest(const std::string& password);
};

RespValuePtr makeError(const std::string& error);

// Construct a RespValue holding a BulkString. Convenience factory used by code
// that builds RESP request/reply payloads piecemeal (e.g., HELLO replies,
// canned commands sent from the conn pool).
RespValue makeBulkString(absl::string_view value);

// Construct a RespValue holding an Integer.
RespValue makeInteger(int64_t value);

// Build a RESP command request (Array of BulkStrings) from a command name
// followed by its argument list. Mirrors the canonical "command + args" shape
// every Redis request takes on the wire and saves callers from open-coding
// the loop in AuthRequest / ReadOnlyRequest / AskingRequest etc.
RespValue makeRequest(absl::string_view command, const std::vector<std::string>& args);

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
