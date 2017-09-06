#include "common/protobuf/utility.h"

#include "common/common/assert.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"

#include "fmt/format.h"

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
  const std::string contents = Filesystem::fileReadToEnd(path);
  // If the filename ends with .pb, attempt to parse it as a binary proto.
  if (StringUtil::endsWith(path, ".pb")) {
    // Attempt to parse the binary format.
    if (message.ParseFromString(contents)) {
      return;
    }
    throw EnvoyException("Unable to parse file \"" + path + "\" as a binary protobuf (type " +
                         message.GetTypeName() + ")");
  }
  // If the filename ends with .pb_text, attempt to parse it as a text proto.
  if (StringUtil::endsWith(path, ".pb_text")) {
    if (Protobuf::TextFormat::ParseFromString(contents, &message)) {
      return;
    }
    throw EnvoyException("Unable to parse file \"" + path + "\" as a text protobuf (type " +
                         message.GetTypeName() + ")");
  }
  if (StringUtil::endsWith(path, ".yaml")) {
    const std::string json = Json::Factory::loadFromYamlString(contents)->asJsonString();
    loadFromJson(json, message);
  } else {
    loadFromJson(contents, message);
  }
}

std::string MessageUtil::getJsonStringFromMessage(const Protobuf::Message& message) {
  Protobuf::util::JsonPrintOptions json_options;
  // By default, proto field names are converted to camelCase when the message is converted to JSON.
  // Setting this option makes debugging easier because it keeps field names consistent in JSON
  // printouts.
  json_options.preserve_proto_field_names = true;
  ProtobufTypes::String json;
  const auto status = Protobuf::util::MessageToJsonString(message, &json, json_options);
  // This should always succeed unless something crash-worthy such as out-of-memory.
  RELEASE_ASSERT(status.ok());
  return json;
}

void MessageUtil::jsonConvert(const Protobuf::Message& source, Protobuf::Message& dest) {
  // TODO(htuch): Consolidate with the inflight cleanups here.
  Protobuf::util::JsonOptions json_options;
  ProtobufTypes::String json;
  const auto status = Protobuf::util::MessageToJsonString(source, &json, json_options);
  // This should always succeed unless something crash-worthy such as out-of-memory.
  RELEASE_ASSERT(status.ok());
  MessageUtil::loadFromJson(json, dest);
}

} // namespace Envoy
