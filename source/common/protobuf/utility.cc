#include "common/protobuf/utility.h"

#include "common/common/assert.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/json/json_loader.h"

#include "spdlog/spdlog.h"

namespace Envoy {

MissingFieldException::MissingFieldException(const std::string& field_name,
                                             const Protobuf::Message& message)
    : EnvoyException(
          fmt::format("Field '{}' is missing in: {}", field_name, message.DebugString())) {}

void MessageUtil::loadFromJson(const std::string& json, Protobuf::Message& message) {
  const auto status = Protobuf::util::JsonStringToMessage(json, &message);
  if (!status.ok()) {
    throw EnvoyException("Unable to parse JSON as proto (" + status.ToString() + "): " + json);
  }
}

void MessageUtil::loadFromFile(const std::string& path, Protobuf::Message& message) {
  loadFromJson(Filesystem::fileReadToEnd(path), message);
}

Json::ObjectSharedPtr WktUtil::getJsonObjectFromStruct(const Protobuf::Struct& message) {
  Protobuf::util::JsonOptions json_options;
  ProtobufTypes::String json;
  const auto status = Protobuf::util::MessageToJsonString(message, &json, json_options);
  // This should always succeed unless something crash-worthy such as out-of-memory.
  RELEASE_ASSERT(status.ok());
  return Json::Factory::loadFromString(json);
}

} // namespace Envoy
