#pragma once

#include <string>

#include "source/common/json/json_streamer.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Json {

class Utility {
public:
  /**
   * Convert a Protobuf::Value to a JSON string.
   * @param value message of type type.googleapis.com/google.protobuf.Value
   * @param dest JSON string.
   */
  static void appendValueToString(const Protobuf::Value& value, std::string& dest);
};

} // namespace Json
} // namespace Envoy
