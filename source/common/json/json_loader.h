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
   * @brief Constructs a Json Object from a string.
   * @param json The JSON string to load.
   * @param ignore_comments Whether to ignore comments in the JSON string. Default is false.
   * Throws Json::Exception if unable to parse the string.
   */
  static ObjectSharedPtr loadFromString(const std::string& json, bool ignore_comments = false);

  /**
   * @brief Loads a JSON object from a string without throwing an exception.
   *
   * @param json The JSON string to load.
   * @param ignore_comments Whether to ignore comments in the JSON string. Default is false.
   * @return An absl::StatusOr<ObjectSharedPtr> representing the loaded JSON object or an error
   * status.
   */
  static absl::StatusOr<ObjectSharedPtr> loadFromStringNoThrow(const std::string& json,
                                                               bool ignore_comments = false);

  /**
   * Constructs a Json Object from a Protobuf struct.
   */
  static ObjectSharedPtr loadFromProtobufStruct(const ProtobufWkt::Struct& protobuf_struct);

  /*
   * Serializes a JSON string to a byte vector using the MessagePack serialization format.
   * If the provided JSON string is invalid, an empty vector will be returned.
   * See: https://github.com/msgpack/msgpack/blob/master/spec.md
   */
  static std::vector<uint8_t> jsonToMsgpack(const std::string& json);
};

} // namespace Json
} // namespace Envoy
