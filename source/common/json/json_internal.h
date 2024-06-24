#pragma once

#include <list>
#include <string>

#include "envoy/json/json_object.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {
namespace Nlohmann {

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

  /**
   * Serializes a string in JSON format, throwing an exception if not valid UTF-8.
   *
   * @param The raw string -- must be in UTF-8 format.
   * @return A string suitable for inclusion in a JSON stream, including double-quotes.
   */
  static std::string serialize(absl::string_view str);

  /*
   * Serializes a JSON string to a byte vector using the MessagePack serialization format.
   * If the provided JSON string is invalid, an empty vector will be returned.
   * See: https://github.com/msgpack/msgpack/blob/master/spec.md
   */
  static std::vector<uint8_t> jsonToMsgpack(const std::string& json);
};

} // namespace Nlohmann
} // namespace Json
} // namespace Envoy
