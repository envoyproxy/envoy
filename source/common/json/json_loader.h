#pragma once

#include <string>

#include "envoy/json/json_object.h"

#include "source/common/common/statusor.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Json {

class Factory {
public:
  /**
   * Constructs a Json Object from a string.
   * Throws Json::Exception if unable to parse the string.
   */
  static ObjectSharedPtr loadFromString(const std::string& json);

  /**
   * Constructs a Json Object from a string.
   */
  static absl::StatusOr<ObjectSharedPtr> loadFromStringNoThrow(const std::string& json);

  /**
   * Constructs a Json Object from a Protobuf struct.
   */
  static ObjectSharedPtr loadFromProtobufStruct(const ProtobufWkt::Struct& protobuf_struct);
};

} // namespace Json
} // namespace Envoy
