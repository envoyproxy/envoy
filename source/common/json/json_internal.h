#pragma once

#include <list>
#include <string>

#include "envoy/json/json_object.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {

class Utility {
public:
  /**
   * Determines whether a string requires escaping as a JSON key or string value.
   * @param str the string to check.
   * @return true if the string requires escaping, false otherwise.
   */
  static bool requireEscaping(absl::string_view str);

  /**
   * Determines whether a char requires escaping as in JSON key or string value.
   * @param c the character to check.
   * @return true if the character requires escaping, false otherwise.
   */
  static bool requireEscaping(char c);

  /**
   * Class to hold the escaped result of a string.
   */
  class EscapedString {
  public:
    EscapedString(absl::string_view str) : escaped_string_(str) {}

    absl::string_view toStringView() const;

  private:
    friend class Envoy::Json::Utility;

    EscapedString(std::string str) : escaped_string_(std::move(str)) {
      ASSERT(absl::get<std::string>(escaped_string_).size() >= 2);
      ASSERT(absl::get<std::string>(escaped_string_).front() == '"');
      ASSERT(absl::get<std::string>(escaped_string_).back() == '"');
    }

    absl::variant<absl::string_view, std::string> escaped_string_;
  };

  /**
   * Escapes a string for use as a JSON key or string value. The output may be the
   * original string view if no escaping is required. Or it may be a new string
   * containing the escaped version of the input string and quotes.
   * Callers should use the toStringView() method of result to get the escaped view.
   * @param str the string to escape.
   * @return the escaped string.
   */
  static EscapedString escape(absl::string_view str);
};

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
   * @param str string to be serialized.
   * @param replace_invalid_utf8 if true, invalid UTF-8 sequences will be replaced with the
   * unicode ``U+FFFD``. Or the invalid UTF-8 will result in an exception.
   * NOTE: THIS MUST BE TRUE FOR SCENARIO WHERE THE EXCEPTION IS DISABLED.
   * @return A string suitable for inclusion in a JSON stream, including double-quotes.
   */
  static std::string serialize(absl::string_view str, bool replace_invalid_utf8);

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
