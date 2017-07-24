#include "envoy/json/json_object.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

class JsonUtil {
public:
  /**
   * Set UInt32 field in a protobuf message based on the corresponding field in a JSON object.
   * @param json source JSON object.
   * @param message destination protobuf message.
   * @param field_name field name.
   */
  static void setUInt32Value(const Json::Object& json, Protobuf::Message& message,
                             const std::string& field_name);

  /**
   * Set Duration field in a protobuf message based on the corresponding field in a JSON object.
   * @param json source JSON object.
   * @param message destination protobuf message.
   * @param field_name field name.
   */
  static void setDuration(const Json::Object& json, Protobuf::Message& message,
                          const std::string& field_name);
};

} // namespace Config
} // namespace Envoy
